#include <kernel/net/icmp.hpp>
#include <kernel/net/ip.hpp>
#include <kernel/net/net.hpp>
#include <stdio.h>

constexpr uint8_t ICMP_ECHO_REPLY = 0x0;
constexpr uint8_t ICMP_ECHO_REQUEST = 0x8;

void handle_echo_request(uint32_t src_ip, char* buffer, uint16_t size) {
    *reinterpret_cast<uint8_t*>(buffer) = ICMP_ECHO_REPLY;
    *reinterpret_cast<uint16_t*>(buffer + 2) = 0; // 清零，再算校验和
    uint16_t chksum = checksum(buffer, size);
    *reinterpret_cast<uint16_t*>(buffer + 2) = chksum;
    send_ipv4(src_ip, IP_PROTOCOL_ICMP, buffer, size);
}

void icmp_handler(uint32_t src_ip, char* buffer, uint16_t size) {
    if (checksum(buffer, size) != 0) {
        return;
    }
    // 根据类型做分类
    uint8_t type = *(reinterpret_cast<uint8_t*>(buffer));
    if (type == ICMP_ECHO_REQUEST) { // Echo Request
        handle_echo_request(src_ip, buffer, size);
    }
}
