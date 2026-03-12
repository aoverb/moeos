#include <net/net.hpp>
#include <net/socket.hpp>
#include <sys/wait.h>
#include <file.h>
#include <stdio.h>
#include <format.h>
#include <stdlib.h>
#include <poll.h>

constexpr char IAC  = (char)255;
constexpr char DONT = (char)254;
constexpr char DO   = (char)253;
constexpr char WONT = (char)252;
constexpr char WILL = (char)251;
constexpr char SB   = (char)250;
constexpr char SE   = (char)240;

int execute_shell(int stdin, int stdout, char*& buf, int& fd) {
    fd = open("/usr/bin/shell", O_RDONLY);
    if (fd == -1) {
        return -1;
    }
    char* buffer = (char*)malloc(131072);
    int size = read(fd, buffer, 131072);
    fd_remap remap[2] = {
        {.child_fd = 0, .parent_fd = stdin},
        {.child_fd = 1, .parent_fd = stdout},
    };
    char* name[] = { "telnet_shell", nullptr };
    int shell_pid = exec(buffer, size, 1, name, remap, 2);
    if (shell_pid <= 0) {
        free(buffer);
        close(fd);
        return -1;
    }
    buf = buffer;
    return shell_pid;
}
static char out_buff[1800];

static char console_buff[500];
void handle_console(int in, int out) {
    // 把控制台的信息经过处理后传给对端
    // 主要是把/n换成/r/n
    int len;
    if ((len = read(in, console_buff, 256)) > 0) {
        
        int outlen = 0;
        for (int i = 0; i < len; ++i) {
            out_buff[outlen] = console_buff[i];
            if (out_buff[outlen] == '\n') {
                out_buff[outlen] = '\r';
                out_buff[++outlen] = '\n';
            }
            outlen++;
        }
        write(out, out_buff, outlen);
    }
}

void handle_conn_out(char* in, int in_len, char* iac, int& iac_len, char* con, int& con_len) {
    iac_len = 0;
    con_len = 0;
    for (int i = 0; i < in_len; ) {
        if (in[i] == IAC) {
            if (in_len - i < 3) break;
            if (in[i + 1] == DO || in[i + 1] == DONT) { // 用WONT拒绝
                iac[iac_len++] = IAC;
                iac[iac_len++] = WONT;
                iac[iac_len++] = in[i + 2];
                i += 3;
            } else if (in[i + 1] == WILL || in[i + 1] == WONT) { // 用DONT拒绝
                iac[iac_len++] = IAC;
                iac[iac_len++] = DONT;
                iac[iac_len++] = in[i + 2];
                i += 3;
            } else if (in[i + 1] == SB) { // 子协商，直接丢掉
                while (i + 1 < in_len && !(in[i] == IAC && in[i + 1] == SE)) {
                    ++i;
                }
                i += 2;
            } else {
                i += 2; // 二字节的IAC
            }
        } else {
            if (in[i] == '\r') {
                con[con_len++] = '\n';
                ++i;
            } else {
                con[con_len++] = in[i];
            }
            ++i;
        }
    }
}

static char conn_buff[1500];

static char iac_buff[1500];
static char output_buff[1500];
void handle_conn(int in, int out) {
    // 主要是处理协商字符，以及把/r/n转成/n
    int len;
    if ((len = read(in, conn_buff, 256)) > 0) {
        
        int iac_outlen = 0;
        int outlen = 0;
        handle_conn_out(conn_buff, len, iac_buff, iac_outlen, output_buff, outlen);
        if (iac_outlen > 0) {
            write(in, iac_buff, iac_outlen); // 拒绝协商写给telnet
        }
        if (outlen > 0) {
            write(out, output_buff, outlen);
        }
    }
}

void handle_session(int conn) {
    int shell_in[2]; // 读端：控制台的标准输入；写端：我们控制
    int shell_out[2]; // 读端：我们拿着；写端：控制台的标准输出

    int ret = pipe(shell_in);
    int ret2 = pipe(shell_out);
    if (ret == -1 || ret2 == -1) {
        close(shell_in[0]);
        close(shell_in[1]);
        close(shell_out[0]);
        close(shell_out[1]);
        return;
    }
    // 我们从TCP连接拿到数据，处理后传给shell
    char* buffer;
    int fd;
    int shell_pid = execute_shell(shell_in[0], shell_out[1], buffer, fd);
    if (shell_pid == -1 || ret == -1 || ret2 == -1) {
        close(shell_in[0]);
        close(shell_in[1]);
        close(shell_out[0]);
        close(shell_out[1]);
        close(conn);
        return;
    }
    pollfd fds[2] = {
        { .fd = shell_out[0], .events = POLLIN, .revents = 0}, // 标准输入
        { .fd = conn, .events = POLLIN, .revents = 0 }
    };

    while(1) {
        int ret = poll(fds, 2, -1);  // -1 = 无限等待
        if (ret < 0) { break; }

        // 控制台有输出了
        if (fds[0].revents & POLLIN) {
            handle_console(shell_out[0], conn);
        }

        // 远端来数据了
        if (fds[1].revents & POLLIN) {
            handle_conn(conn, shell_in[1]);
        }
        fds[0].revents = 0;
        fds[1].revents = 0;
    }
    waitpid(shell_pid);
    free(buffer);
    close(fd);
    close(conn);
}

int main(int argc, char** argv) {
    int conn = open("/sock/tcp", O_CREATE);
    if (conn == -1) {
        printf("telnetd: tcp unsupported!\n");
        return 0;
    }

    sockaddr bindaddr;
    bindaddr.addr = SOCKADDR_BROADCAST_ADDR;
    bindaddr.port = 23;
    if (ioctl(conn, "SOCK_IOC_BIND", &bindaddr) < 0) {
        printf("telnetd: failed to bind 0.0.0.0:23!\n");
        return 0;
    }

    if (listen(conn, 5)) {
        printf("telnetd: failed to listen!\n");
        return 0;
    }
    int client_fd;
    while (client_fd = accept(conn, nullptr, nullptr)) {
        if (client_fd == -1) break;
        handle_session(client_fd);
    }

    close(conn);
    return 0;
}