#include <file.h>
#include <stdio.h>
#include <string.h>
#include <format.h>
#include <stdlib.h>
#include <time.hpp>
#include <net/net.hpp>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: ping <ip addr>\n");
        return 0;
    }
    char ip_addr[16];
    snprintf(ip_addr, sizeof(ip_addr), "%s", argv[1]);

    char icmp_open_path[64];
    snprintf(icmp_open_path, sizeof(icmp_open_path), "/sock/%s/icmp", ip_addr);

    int file = open(icmp_open_path, O_CREATE);
    if (file == -1) {
        printf("icmp unsupported!\n");
        return 0;
    }

    // 构造ICMP报文
    const char* data_str = "1234567890loliOS$!@&#($&)OWOXCVBNMASDFGHJKLQWERTYUIOPBA";

    size_t payload_size = sizeof(icmp_echo_head) + sizeof(char) * (strlen(data_str) + 1);
    void* payload = malloc(payload_size);
    icmp_echo_head* head = (icmp_echo_head*)payload;
    memcpy(payload + sizeof(icmp_echo_head), data_str, payload_size - sizeof(icmp_echo_head));
    head->type = ICMP_ECHO_REQUEST;
    head->code = 0;

    printf("PING %s %d bytes of data.\n", ip_addr, payload_size);

    int ping_count = 4;
    int transmitted = 0;
    int received = 0;
    int rtt_min = 0x7FFFFFFF;
    int rtt_max = 0;
    int rtt_sum = 0;

    char* buffer = (char*)malloc(sizeof(char) * 2048);
    for (int i = 0; i < ping_count; ++i) {
        clock_t ticks = clock();

        head->id = file;
        head->seq = i;
        transmitted++;

        uint32_t recv_size = 0;
        if (write(file, (char*)payload, sizeof(icmp_echo_head) + strlen(data_str) + 1) == -1) {
            printf("Failed to send icmp request.\n");
        } else if ((recv_size = read(file, buffer, 2048)) != -1) {
            auto* ip = reinterpret_cast<ip_header*>(buffer);
            if (ip->header_len * 4 + sizeof(icmp_echo_head) > recv_size) {
                printf("Malformed packet.\n");
                continue;
            }
            if (checksum(buffer + ip->header_len * 4, recv_size - ip->header_len * 4) != 0) {
                printf("Packet checksum error.\n");
                continue;
            }
            int rtt = (clock() - ticks) * 10;
            received++;
            rtt_sum += rtt;
            if (rtt < rtt_min) rtt_min = rtt;
            if (rtt > rtt_max) rtt_max = rtt;

            printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%dms\n", recv_size, ip_addr,
                reinterpret_cast<icmp_echo_head*>(buffer + ip->header_len * 4)->seq,
                ip->ttl, rtt);
        } else {
            printf("Request timed out.\n");
        }
        if (i != ping_count - 1) sleep(1);
    }

    // 统计信息
    printf("\n--- %s ping statistics ---\n", ip_addr);
    int loss = ((transmitted - received) * 100) / (transmitted > 0 ? transmitted : 1);
    printf("%d packets transmitted, %d received, %d%% packet loss\n",
        transmitted, received, loss);
    if (received > 0) {
        printf("rtt min/avg/max = %d/%d/%d ms\n",
            rtt_min, rtt_sum / received, rtt_max);
    }

    free(buffer);
    free(payload);
    close(file);
    return 0;
}