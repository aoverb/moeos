#ifndef _KERNEL_NET_SOCKET_HPP
#define _KERNEL_NET_SOCKET_HPP

#include <kernel/net/net.hpp>
#include <net/socket.hpp>
#include <kernel/spinlock.hpp>

#ifdef __cplusplus
extern "C" {
#endif
struct mounting_point;
struct spinlock;
struct PCB;
typedef PCB* process_queue;
enum class protocol {ROOT, ICMP, TCP};

typedef struct {
    uint8_t valid;
    protocol ptcl;
    char src_addr[32];
    char dst_addr[32];
    uint16_t src_port;
    uint16_t dst_port;
    void* data;
    spinlock lock;
    process_queue wait_queue;
} socket;

struct sock_operation {
    int (*connect)(mounting_point* mp, uint32_t inode_id, const char* addr, uint16_t port);
    int (*listen)(mounting_point* mp, uint32_t inode_id, size_t queue_length);
    int (*accept)(mounting_point* mp, uint32_t inode_id, sockaddr* peeraddr, size_t* size);
};
#ifdef __cplusplus
}
#endif

#endif