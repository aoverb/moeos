#ifndef _KERNEL_NET_IP_HPP
#define _KERNEL_NET_IP_HPP

#include <stdint.h>
#include <net/net.hpp>
#include <kernel/net/net.hpp>

#ifdef __cplusplus
extern "C" {
#endif

int send_ipv4(const ipv4addr& dst_ip, uint8_t protocol,
    const void* payload, uint32_t payload_len, uint8_t ttl = 128);

#ifdef __cplusplus
}
#endif

#endif