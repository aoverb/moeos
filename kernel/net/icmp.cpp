#include <kernel/net/icmp.hpp>
#include <kernel/net/net.hpp>
#include <stdio.h>

constexpr uint8_t ICMP_ECHO_REQUEST = 0x8;

void handle_echo_request(char* buffer, uint16_t size) {
    printf("icmp received ICMP_ECHO_REQUEST\n");
}

void icmp_handler(char* buffer, uint16_t size) {
    printf("icmp received packet size %d.\n", checksum(buffer, size));
    if (checksum(buffer, size) != 0) {
        return;
    }
    // 根据类型做分类
    uint8_t type = *(reinterpret_cast<uint8_t*>(buffer));
    if (type == ICMP_ECHO_REQUEST) { // Echo Request
        handle_echo_request(buffer, size);
    }
}
