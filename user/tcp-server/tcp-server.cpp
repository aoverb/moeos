#include <net/net.hpp>
#include <net/socket.hpp>
#include <file.h>
#include <stdio.h>
#include <format.h>
#include <stdlib.h>
#include <poll.h>

int main(int argc, char** argv) {
    int conn = open("/sock/tcp", O_CREATE);
    if (conn == -1) {
        printf("tcp unsupported!\n");
        return 0;
    }
    sockaddr bindaddr;
    bindaddr.addr = SOCKADDR_BROADCAST_ADDR;
    bindaddr.port = 8080;
    if (ioctl(conn, "SOCK_IOC_BIND", &bindaddr) < 0) {
        printf("failed to bind %s:%d", bindaddr.addr, bindaddr.port);
        return 0;
    }

    if (listen(conn, 5)) {
        printf("failed to listen!\n");
    }

    int client_fd;
    while (client_fd = accept(conn, nullptr, nullptr)) {
        if (client_fd == -1) break;
        pollfd fds[2] = {
            { .fd = 0, .events = POLLIN, .revents = 0}, // 标准输入
            { .fd = client_fd, .events = POLLIN, .revents = 0 }
        };

        char buff[256];
        while(1) {
            int ret = poll(fds, 2, -1);  // -1 = 无限等待
            if (ret < 0) { break; }

            if (fds[0].revents & POLLIN) {
                uint32_t n = read(0, buff, sizeof(buff));
                if (n <= 0) break;
                write(client_fd, buff, n);
            }

            // socket 有数据 → 读取并打印
            if (fds[1].revents & POLLIN) {
                uint32_t n = read(client_fd, buff, sizeof(buff));
                if (n <= 0) {
                    printf("connection has been closed\n");
                    break;
                }
                buff[n] = '\0';
                printf("%s\n", buff);
            }
        }
    }

    close(conn);
    return 0;
}