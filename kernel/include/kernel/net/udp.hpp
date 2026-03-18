#ifndef _KERNEL_NET_UDP_HPP
#define _KERNEL_NET_UDP_HPP

#include <kernel/net/ip.hpp>
#include <kernel/net/socket.hpp>
#include <shared_ptr>
#include <queue>
#ifdef __cplusplus
extern "C" {
#endif

int udp_init(socket& sock, uint16_t local_port);
int udp_read(socket& sock, char* buffer, uint32_t size);
int udp_write(socket& sock, const char* payload, uint32_t size);
int udp_ioctl(socket& sock, uint32_t request, void* arg);
int udp_connect(socket& sock, uint32_t addr, uint16_t port);
int udp_sendto(socket& sock, const char* buffer, uint32_t size, sockaddr* peeraddr);
int udp_recvfrom(socket& sock, char* buffer, uint32_t size, sockaddr* peeraddr);
int udp_close(socket& sock);

#ifdef __cplusplus
}
#endif

#endif