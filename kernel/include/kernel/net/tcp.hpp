#ifndef _KERNEL_NET_TCP_HPP
#define _KERNEL_NET_TCP_HPP

#include <kernel/net/ip.hpp>
#include <kernel/net/socket.hpp>
#include <shared_ptr>
#include <queue>
#ifdef __cplusplus
extern "C" {
#endif
constexpr uint8_t MAX_ACCEPTED_QUEUE_NUM = 128;

struct TCB; // 前向声明
using TCBPtr = shared_ptr<TCB>;

void delete_procfs_inode(TCB* tcb);

struct TCB { // 传输控制块
    tcb_state state;
    char* window;
    size_t window_size;
    size_t window_head;
    size_t window_tail;
    size_t window_used_size;
    uint32_t seq;
    uint32_t ack;
    std::queue<TCBPtr, MAX_ACCEPTED_QUEUE_NUM> accepted_queue;
    uint8_t accepted_queue_size;
    uint8_t pending_count = 0;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint16_t src_port;
    uint16_t dst_port;

    socket* owner;
    socket* listener;
    spinlock lock;

    uint32_t procfs_inode_id;
    // 析构时自动释放接收窗口
    ~TCB() {
        if (window) {
            kfree(window);
            window = nullptr;
        }
        delete_procfs_inode(this);
    }
};

int tcp_init(socket& sock, uint16_t local_port);
int tcp_connect(socket& sock, uint32_t addr, uint16_t port);
int tcp_read(socket& sock, char* buffer, uint32_t size);
int tcp_write(socket& sock, char* buffer, uint32_t size);
int tcp_listen(socket& sock, size_t queue_length);
TCBPtr tcp_accept(socket& sock, sockaddr* peeraddr, size_t* size);
int tcp_ioctl(TCBPtr& tcb, uint32_t request, void* arg);
int tcp_close(socket& sock);

void set_tcb_mp(mounting_point* mp);

#ifdef __cplusplus
}
#endif

#endif