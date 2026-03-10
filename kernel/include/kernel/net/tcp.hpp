#ifndef _KERNEL_NET_TCP_HPP
#define _KERNEL_NET_TCP_HPP

#include <kernel/net/ip.hpp>
#include <kernel/net/socket.hpp>
#ifdef __cplusplus
extern "C" {
#endif

int tcp_init(socket& sock, uint16_t local_port);
int tcp_connect(socket& sock, const char* addr, uint16_t port);
#ifdef __cplusplus
}
#endif

#endif