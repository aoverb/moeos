#include <net/net.hpp>
#include <net/socket.hpp>
#include <file.h>
#include <stdio.h>
#include <format.h>
#include <stdlib.h>
#include <poll.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: tcp-sender <ip addr> <port>\n");
        return 0;
    }

    int conn = open("/sock/tcp", O_CREATE);
    if (conn == -1) {
        printf("tcp unsupported!\n");
        return 0;
    }

    char ip_addr[16];
    uint16_t dst_port;
    snprintf(ip_addr, sizeof(ip_addr), "%s", argv[1]);
    dst_port = atoi(argv[2]);

    printf("tcp-sender: connecting to %s:%d using port %d...", ip_addr, dst_port);
    if (connect(conn, ip_addr, dst_port)) {
        printf("connection establised failed!\n");
        return 0;
    }

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