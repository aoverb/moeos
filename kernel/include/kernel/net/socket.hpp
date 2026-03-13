#ifndef _KERNEL_NET_SOCKET_HPP
#define _KERNEL_NET_SOCKET_HPP

#include <kernel/net/net.hpp>
#include <net/socket.hpp>
#include <shared_ptr>
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
enum class protocol {ROOT, ICMP, TCP, UDP};

extern "C++" {
using TCBPtr = shared_ptr<TCB>;

struct udp_pack {
    uint32_t size;
    uint32_t remote_ip;
    uint32_t remote_port;
    udp_pack* next;
    void* data;
};

struct socket {
    uint16_t inode_id;
    uint8_t valid;
    protocol ptcl;
    union {
        struct { uint32_t   bound_ip;
                 icmp_node* queue_head;  }   icmp;

        struct { TCBPtr     block;       }   tcp;

        struct { uint32_t   local_ip;
                 uint32_t   remote_ip;
                 uint16_t   local_port;
                 uint16_t   remote_port;
                 udp_pack*  pack_head;
                 udp_pack*  pack_tail;   }   udp;
    } data;
    spinlock lock;
    process_queue wait_queue;
    process_queue* poll_queue;
}; // 全部都放网络序！
}

void wake_all_queue(socket* sock);

struct sock_operation {
    int (*connect)(mounting_point* mp, uint32_t inode_id, const char* addr, uint16_t port);
    int (*listen)(mounting_point* mp, uint32_t inode_id, size_t queue_length);
    int (*accept)(mounting_point* mp, uint32_t inode_id, sockaddr* peeraddr, size_t* size);
    int (*sendto)(mounting_point* mp, uint32_t inode_id, const char* buffer, uint32_t size, sockaddr* peeraddr);
    int (*recvfrom)(mounting_point* mp, uint32_t inode_id, char* buffer, uint32_t size, sockaddr* peeraddr);
};
#ifdef __cplusplus
}
#endif

#endif