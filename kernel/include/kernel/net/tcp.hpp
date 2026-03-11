#ifndef _KERNEL_NET_TCP_HPP
#define _KERNEL_NET_TCP_HPP

#include <kernel/net/ip.hpp>
#include <kernel/net/socket.hpp>
#include <queue>
#ifdef __cplusplus
extern "C" {
#endif
constexpr uint8_t MAX_ACCEPTED_QUEUE_NUM = 128;
struct TCB { // 传输控制块
    tcb_state state;
    char* window;
    size_t window_size;
    size_t window_head;
    size_t window_tail;
    size_t window_used_size;
    uint32_t seq;
    uint32_t ack;
    std::queue<TCB*, MAX_ACCEPTED_QUEUE_NUM> accepted_queue;
    uint8_t accepted_queue_size;
    uint8_t pending_count = 0;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint16_t src_port;
    uint16_t dst_port;

    socket* owner;
    socket* listener;
    spinlock lock;
};

int tcp_init(socket& sock, uint16_t local_port);
int tcp_connect(socket& sock, uint32_t addr, uint16_t port);
int tcp_read(socket& sock, char* buffer, uint32_t size);
int tcp_listen(socket& sock, size_t queue_length);
TCB* tcp_accept(socket& sock, sockaddr* peeraddr, size_t* size);
int tcp_ioctl(TCB* tcb, const char* cmd, void* arg);

#ifdef __cplusplus
}
#endif

#endif