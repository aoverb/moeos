#ifndef _KERNEL_NET_ARP_HPP
#define _KERNEL_NET_ARP_HPP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

constexpr uint16_t APR_OPCODE_REQ = 1;
constexpr uint16_t APR_OPCODE_REPLY = 2;

int send_arp(uint16_t opcode, const uint8_t* target_mac, const uint8_t* target_ip);

#ifdef __cplusplus
}
#endif

#endif