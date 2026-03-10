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
struct TCB;
struct icmp_node;
typedef PCB* process_queue;
enum class protocol {ROOT, ICMP, TCP};

struct socket {
    uint8_t valid;
    protocol ptcl;
    union {
        struct { uint32_t bound_ip;
                 icmp_node* queue_head; }   icmp;
        struct { TCB* block; }        tcp;
    } data;
    spinlock lock;
    process_queue wait_queue;
}; // 全部都放网络序！

struct sock_operation {
    int (*connect)(mounting_point* mp, uint32_t inode_id, const char* addr, uint16_t port);
    int (*listen)(mounting_point* mp, uint32_t inode_id, size_t queue_length);
    int (*accept)(mounting_point* mp, uint32_t inode_id, sockaddr* peeraddr, size_t* size);
};
#ifdef __cplusplus
}
#endif

#endif