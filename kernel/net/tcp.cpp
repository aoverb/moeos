#include <kernel/net/tcp.hpp>
#include <kernel/mm.h>
#include <kernel/process.hpp>
#include <kernel/timer.hpp>
#include <kernel/schedule.h>
#include <format.h>
#include <unordered_map>

struct TCB { // 传输控制块
    tcb_state state;
    char* window;
    size_t window_size;
    size_t window_tail;
    uint32_t seq;
    uint32_t ack; 
};

// 约定：网络序存储
struct tcp_quadruple {
    uint32_t local_ip;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    bool operator==(const tcp_quadruple& o) const {
        return local_ip == o.local_ip && remote_ip == o.remote_ip &&
               local_port == o.local_port && remote_port == o.remote_port;
    }
} __attribute__((packed));

struct tcp_hasher {
    size_t operator()(const tcp_quadruple& q) const {
        size_t seed = 0;
        auto combine = [&](uint32_t v) {
            // 经典的位扰动算法，防止哈希冲突
            seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };
        combine(q.local_ip);
        combine(q.remote_ip);
        combine((uint32_t)q.local_port  << 16 | q.remote_port);
        return seed;
    }
};

std::unordered_map<tcp_quadruple, socket*, tcp_hasher> map_to_sock;

constexpr uint32_t DEFAULT_WINDOW_SIZE = (1 << 16) - 1;
constexpr uint32_t DEFAULT_WINDOW_SCALE = 1;

int tcp_init(socket& sock, uint16_t local_port) {
    sock.ptcl = protocol::TCP;
    strcpy(sock.dst_addr, "127.0.0.1"); // 先给一个默认地址，后面通过connect设置
    sock.dst_port = 0; // 与上面同理

    uint8_t src_addr[4];
    getLocalNetconf()->ip.to_bytes(src_addr);
    sprintf(sock.src_addr, "%d.%d.%d.%d", src_addr[0], src_addr[1], src_addr[2], src_addr[3]);
    sock.src_port = local_port;

    sock.data = kmalloc(sizeof(TCB));
    TCB* tcb = (TCB*)sock.data;
    tcb->state = tcb_state::CLOSED;
    tcb->window_size = DEFAULT_WINDOW_SIZE * DEFAULT_WINDOW_SCALE;
    tcb->window = (char*)kmalloc(tcb->window_size);
    tcb->window_tail = 0;
    tcb->seq = 0; // todo: 这里要使用时间戳+随机数生成
    tcb->ack = 0;
    return 0;
}

int send_tcp_pack(socket& sock, tcp_flags flags, const char* payload, size_t size) {
    void* packet = kmalloc(sizeof(pseudo_tcp_header) + sizeof(tcp_header) + size);
    uint32_t packet_size = sizeof(pseudo_tcp_header) + sizeof(tcp_header) + size;
    memset(packet, 0, packet_size);
    pseudo_tcp_header* p_header = (pseudo_tcp_header*)packet;
    tcp_header* t_header = (tcp_header*)((char*)packet + sizeof(pseudo_tcp_header));
    TCB* tcb = (TCB*)sock.data;
    int tmp[4];

    sscanf_s(sock.dst_addr, "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
    uint8_t dst_addr[4] = { (uint8_t)tmp[0], (uint8_t)tmp[1], (uint8_t)tmp[2], (uint8_t)tmp[3] };

    sscanf_s(sock.src_addr, "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
    uint8_t src_addr[4] = { (uint8_t)tmp[0], (uint8_t)tmp[1], (uint8_t)tmp[2], (uint8_t)tmp[3] };
    p_header->src_addr = ipv4addr(src_addr).addr;
    p_header->dst_addr = ipv4addr(dst_addr).addr;
    p_header->protocol = IP_PROTOCOL_TCP;
    p_header->zero = 0;
    p_header->tcp_length = htons(sizeof(tcp_header) + size);

    t_header->src_port = htons(sock.src_port);
    t_header->dst_port = htons(sock.dst_port);
    t_header->seq_num = htonl(tcb->seq);
    t_header->ack_num = htonl(tcb->ack);
    t_header->reserved = 0;
    t_header->data_offset = sizeof(tcp_header) / 4;
    t_header->flags = (uint8_t)flags;
    t_header->window = htons(tcb->window_size);
    t_header->checksum = 0;
    t_header->urgent_ptr = 0;
    memcpy(((char*)packet + sizeof(pseudo_tcp_header) + sizeof(tcp_header)), payload, size);
    t_header->checksum = checksum(packet, packet_size);
    map_to_sock[tcp_quadruple {.local_ip = p_header->src_addr, .remote_ip = p_header->dst_addr,
                               .local_port = t_header->src_port, .remote_port = t_header->dst_port}] = &sock;
    tcb->seq += size;
    if ((uint8_t)flags & (uint8_t)tcp_flags::FIN) {
        tcb->seq += 1; // 虚拟字节
    }
    int ret = send_ipv4((ipv4addr(dst_addr)), IP_PROTOCOL_TCP, t_header, sizeof(tcp_header) + size);
    kfree(packet);
    return ret;
}

int tcp_bind(socket& sock, sockaddr* bind_conf) {
    strcpy(sock.src_addr, bind_conf->addr);
    sock.src_port = bind_conf->port;
    return 0;
}

int tcp_ioctl(socket& sock, const char* cmd, void* arg) {
    if (strcmp(cmd, "SOCK_IOC_BIND") == 0) {
        return tcp_bind(sock, reinterpret_cast<sockaddr*>(arg));
    }
    return -1;
}

int tcp_connect(socket& sock, const char* addr, uint16_t port) {
    TCB* tcb = (TCB*)sock.data;
    uint32_t flags = spinlock_acquire(&(sock.lock));
    strncpy(sock.dst_addr, addr, 16);
    sock.dst_port = port;
    // SEND SYN
    if (send_tcp_pack(sock, tcp_flags::SYN, nullptr, 0) < 0) {
        spinlock_release(&(sock.lock), flags);
        return -1;
    }
    tcb->seq += 1; // 虚拟字节
    tcb->state = tcb_state::SYN_SENT;

    int ret = -1;
    
    if (tcb->state == tcb_state::ESTABLISHED) {
        ret = 0;
    } else {
        {
            SpinlockGuard guard(process_list_lock);
            process_list[cur_process_id]->state = process_state::WAITING;
            insert_into_process_queue(sock.wait_queue, process_list[cur_process_id]);
        }
        spinlock_release(&(sock.lock), flags);
        timeout(&(sock.wait_queue), 3000);
        flags = spinlock_acquire(&(sock.lock));
        if (tcb->state == tcb_state::ESTABLISHED) {
            ret = 0;
        }
    }
    spinlock_release(&(sock.lock), flags);
    return ret;
}

int tcp_listen(socket& sock, size_t queue_length) {
    return -1;
}

int tcp_accept(socket& sock, sockaddr* peeraddr, size_t* size) {
    return -1;
}

void tcp_handler(uint16_t ip_header_size, char* buffer, uint16_t size) {
    tcp_header* header = reinterpret_cast<tcp_header*>(buffer + ip_header_size);
    uint32_t src_ip = reinterpret_cast<ip_header*>(buffer)->src_ip;
    uint32_t dst_ip = reinterpret_cast<ip_header*>(buffer)->dst_ip;
    uint16_t src_port = header->src_port;
    uint16_t dst_port = header->dst_port;

    auto itr = map_to_sock.find(tcp_quadruple {.local_ip = dst_ip, .remote_ip = src_ip,
                               .local_port = dst_port, .remote_port = src_port});
    if (itr == map_to_sock.end()) { // 没在已有的连接找到
        printf("discard."); // todo: 这里可能是被动连接，我们先丢弃。
        return;
    }
    SpinlockGuard guard(itr->second->lock);
    socket* sock = itr->second;
    TCB* tcb = (TCB*)itr->second->data;

    switch (tcb->state)
    {
    case tcb_state::CLOSED:
        return;
    case tcb_state::SYN_SENT:
        if ((header->flags & ((uint8_t)tcp_flags::SYN | (uint8_t)tcp_flags::ACK)) != 
            ((uint8_t)tcp_flags::SYN | (uint8_t)tcp_flags::ACK)) {
                break;
        }
        tcb->ack = ntohl(header->seq_num) + 1; // 下一次希望收到的：SYN出来的序列号，加一个虚拟字节
        send_tcp_pack(*sock, tcp_flags::ACK, nullptr, 0);
        tcb->state = tcb_state::ESTABLISHED;
        {
            SpinlockGuard guard(process_list_lock);
            PCB* cur;
            while(cur = sock->wait_queue) {
                remove_from_process_queue(sock->wait_queue, cur->pid);
                cur->state = process_state::READY;
                insert_into_scheduling_queue(cur->pid);
            }
        }
        break;
    case tcb_state::ESTABLISHED:
        if ((header->flags & (uint8_t)tcp_flags::ACK) == 0) {
            break;
        }
        // todo: 把数据写入缓冲区
        printf("news");
        break;
    default:
        break;
    }
}