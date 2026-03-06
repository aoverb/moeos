#ifndef _KERNEL_NET_ETHERNET_HPP
#define _KERNEL_NET_ETHERNET_HPP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

constexpr uint8_t TYPE_ARP[] = {0x08, 0x06};
constexpr uint8_t TYPE_IP[] = {0x08, 0x00};

int send_ethernet_frame(const uint8_t target_mac[6], const uint8_t source_mac[6], const uint8_t type[2],
    void* buffer, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif