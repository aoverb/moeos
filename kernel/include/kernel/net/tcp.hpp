#ifndef _KERNEL_NET_TCP_HPP
#define _KERNEL_NET_TCP_HPP

#include <kernel/net/ip.hpp>
#include <kernel/net/socket.hpp>
#ifdef __cplusplus
extern "C" {
#endif

int tcp_init(socket& sock, uint16_t local_port);
int tcp_connect(socket& sock, uint32_t addr, uint16_t port);
int tcp_listen(socket& sock, size_t queue_length);
int tcp_accept(socket& sock, sockaddr* peeraddr, size_t* size);
int tcp_ioctl(socket& sock, const char* cmd, void* arg);

#ifdef __cplusplus
}
#endif

#endif