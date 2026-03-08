#include <kernel/net/icmp.hpp>
#include <kernel/net/ip.hpp>
#include <kernel/net/net.hpp>
#include <stdio.h>

void handle_echo_request(const ipv4addr& src_ip, char* buffer, uint16_t size) {
    *reinterpret_cast<uint8_t*>(buffer) = ICMP_ECHO_REPLY;
    *reinterpret_cast<uint16_t*>(buffer + 2) = 0; // 清零，再算校验和
    uint16_t chksum = checksum(buffer, size);
    *reinterpret_cast<uint16_t*>(buffer + 2) = chksum;
    send_ipv4(src_ip, IP_PROTOCOL_ICMP, buffer, size);
}

void icmp_handler(uint16_t ip_header_size, char* buffer, uint16_t size) {
    if (checksum(buffer + ip_header_size - ip_header_size, size) != 0) {
        return;
    }
    // 根据类型做分类
    uint8_t type = *(reinterpret_cast<uint8_t*>(buffer + ip_header_size));
    if (type == ICMP_ECHO_REQUEST) {
        handle_echo_request(ipv4addr(reinterpret_cast<ip_header*>(buffer)->src_ip), buffer + ip_header_size, size - ip_header_size);
    } else if (type == ICMP_ECHO_REPLY) {
        printf("received reply!\n");
    }
}
