#include <file.h>
#include <stdio.h>
#include <string.h>
#include <format.h>
#include <stdlib.h>
#include <time.hpp>

struct icmp_recv_result {
    uint8_t  src_ip[4];
    uint8_t  ttl;
    uint16_t seq;
    uint32_t rtt_ms;
    // 后面跟 payload
};

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: ping <ip addr>\n");
        return 0;
    }
    char ip_addr[16];
    snprintf(ip_addr, sizeof(ip_addr), "%s", argv[1]);

    char icmp_open_path[64];
    snprintf(icmp_open_path, sizeof(icmp_open_path), "/sock/%s/icmp", ip_addr);

    int file = open(icmp_open_path, O_RDWR);
    if (file == -1) {
        printf("icmp unsupported!\n");
        return 0;
    }

    const char* payload = "1234567890loliOS$!@&#($&)OWOXCVBNMASDFGHJKLQWERTYUIOPBA";

    printf("PING %s %d bytes of data.\n", ip_addr, strlen(payload));
    int ping_count = 5;
    char* buffer = (char*)malloc(sizeof(char) * 2048);
    while(ping_count--) {
        
        clock_t ticks = clock();

        uint32_t recv_size = 0;
        if (!write(file, payload, strlen(payload))) {
            printf("Failed to send icmp request.\n");
        } else if ((recv_size = read(file, buffer, 2048))){

            printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%dms\n", recv_size, ip_addr,
                reinterpret_cast<icmp_recv_result*>(buffer)->seq,
                reinterpret_cast<icmp_recv_result*>(buffer)->ttl,
                (clock() - ticks) * 10);
        } else {
            printf("Request timed out.\n");
        }
        sleep(1);
    }
    free(buffer);
    return 0;
}