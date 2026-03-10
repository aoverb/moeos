#ifndef _KERNEL_NET_ARP_HPP
#define _KERNEL_NET_ARP_HPP

#include <stdint.h>
#include <kernel/net/net.hpp>
#include <kernel/net/socket.hpp>

#ifdef __cplusplus
extern "C" {
#endif

struct icmp_node {
    char* data;
    uint32_t size;
    icmp_node* next;
};

int icmp_init(socket& sock);
int icmp_connect(socket& sock, uint32_t addr, uint16_t);
#ifdef __cplusplus
}
#endif

#endif