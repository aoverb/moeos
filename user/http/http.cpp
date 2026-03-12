#include <net/net.hpp>
#include <net/socket.hpp>
#include <file.h>
#include <stdio.h>
#include <format.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: tcp-sender <ip addr> <domain>\n");
        return 0;
    }

    int conn = open("/sock/tcp", O_CREATE);
    if (conn == -1) {
        printf("http unsupported!\n");
        return 0;
    }

    char ip_addr[16];
    snprintf(ip_addr, sizeof(ip_addr), "%s", argv[1]);
    char domain[64];
    snprintf(domain, sizeof(domain), "%s", argv[2]);
    printf("http: connecting to %s:80...", ip_addr);
    if (connect(conn, ip_addr, 80)) {
        printf("connection establised failed!\n");
        return 0;
    }

    pollfd fds[2] = {
        { .fd = 0, .events = POLLIN, .revents = 0}, // 标准输入
        { .fd = conn, .events = POLLIN, .revents = 0 }
    };

    char buff[256];

    char sndbuff[256];
    while(1) {
        int ret = poll(fds, 2, -1);  // -1 = 无限等待
        if (ret < 0) { break; }

        if (fds[0].revents & POLLIN) {
            int n = read(0, buff, sizeof(buff));
            buff[n - 1] = '\0';
            if (n <= 0) break;
            sprintf(sndbuff, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\n\r\n", buff, domain);
            printf("you are sending : %s", sndbuff);
            write(conn, sndbuff, strlen(sndbuff));
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