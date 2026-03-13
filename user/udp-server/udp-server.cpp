#include <net/net.hpp>
#include <net/socket.hpp>
#include <file.h>
#include <stdio.h>
#include <format.h>
#include <stdlib.h>
#include <poll.h>

int main(int argc, char** argv) {
    int conn = open("/sock/udp", O_CREATE);
    if (conn == -1) {
        printf("udp unsupported!\n");
        return 0;
    }
    sockaddr bindaddr;
    bindaddr.addr = SOCKADDR_BROADCAST_ADDR;
    bindaddr.port = htons(9999);
    if (ioctl(conn, "SOCK_IOC_BIND", &bindaddr) < 0) {
        printf("failed to bind %s:%d", ntohl(bindaddr.addr), ntohs(bindaddr.port));
        return 0;
    }
    printf("UDP Server listening...\n");
    pollfd fds[2] = {
        { .fd = 0, .events = POLLIN, .revents = 0}, // 标准输入
        { .fd = conn, .events = POLLIN, .revents = 0 }
    };

    char buff[256];
    while(1) {
        int ret = poll(fds, 2, -1);  // -1 = 无限等待
        if (ret < 0) { break; }

        if (fds[0].revents & POLLIN) {
            int n = read(0, buff, sizeof(buff));
            if (n <= 0) break;
            if (strncmp("/bye\n", buff, n) == 0) {
                break;
            }
            write(conn, buff, n);
        }

        // socket 有数据 → 读取并打印
        if (fds[1].revents & POLLIN) {
            int n = read(conn, buff, sizeof(buff));
            if (n < 0) {
                printf("connection has been closed\n");
                break;
            }
            if (n == 0) continue;
            buff[n] = '\0';
            printf("%s\n", buff);
        }
    }

    close(conn);
    return 0;
}