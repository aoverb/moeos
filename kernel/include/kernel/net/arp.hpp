#ifndef _KERNEL_NET_ARP_HPP
#define _KERNEL_NET_ARP_HPP

#include <stdint.h>
#include <kernel/net/net.hpp>

#ifdef __cplusplus
extern "C" {
#endif

constexpr uint16_t APR_OPCODE_REQ = 1;
constexpr uint16_t APR_OPCODE_REPLY = 2;

bool arp_table_lookup(const ipv4addr& ip, macaddr& mac);
int send_arp(uint16_t opcode, const macaddr target_mac, const ipv4addr target_ip);

#ifdef __cplusplus
}
#endif

#endif