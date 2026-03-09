#ifndef _KERNEL_NET_SOCKET_HPP
#define _KERNEL_NET_SOCKET_HPP

#include <kernel/net/net.hpp>
#include <net/socket.hpp>

#ifdef __cplusplus
extern "C" {
#endif
struct mounting_point;

struct sock_operation {
    int (*connect)(mounting_point* mp, uint32_t inode_id, const char* addr, uint16_t port);
};
#ifdef __cplusplus
}
#endif

#endif