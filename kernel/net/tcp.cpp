#include <kernel/net/tcp.hpp>
#include <kernel/mm.h>
#include <kernel/process.hpp>
#include <kernel/timer.hpp>
#include <format.h>
#include <unordered_map>

struct TCB { // 传输控制块
    tcb_state state;
    char* window;
    size_t window_size;
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
        combine((uint32_t)q.remote_port << 16 | q.remote_port);
        return seed;
    }
};

std::unordered_map<tcp_quadruple, socket*, tcp_hasher> map_to_sock;

constexpr uint32_t DEFAULT_WINDOW_SIZE = (1 << 16) - 1;
constexpr uint32_t DEFAULT_WINDOW_SCALE = 1;

int tcp_init(socket& sock, uint16_t local_port) {
    sock.ptcl = protocol::TCP;
    strcpy("127.0.0.1", sock.dst_addr); // 先给一个默认地址，后面通过connect设置
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

    return 0;
}

int tcp_connect(socket& sock, const char* addr, uint16_t port) {
    // SEND SYN
    void* header = kmalloc(sizeof(pseudo_tcp_header) + sizeof(tcp_header));
    memset(header, 0, sizeof(pseudo_tcp_header) + sizeof(tcp_header));
    pseudo_tcp_header* p_header = (pseudo_tcp_header*)header;
    tcp_header* t_header = (tcp_header*)((char*)header + sizeof(pseudo_tcp_header));
    TCB* tcb = (TCB*)sock.data;
    int tmp[4];

    sscanf_s(addr, "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
    uint8_t dst_addr[4] = { (uint8_t)tmp[0], (uint8_t)tmp[1], (uint8_t)tmp[2], (uint8_t)tmp[3] };

    sscanf_s(sock.src_addr, "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
    uint8_t src_addr[4] = { (uint8_t)tmp[0], (uint8_t)tmp[1], (uint8_t)tmp[2], (uint8_t)tmp[3] };
    p_header->src_addr = ipv4addr(src_addr).addr;
    p_header->dst_addr = ipv4addr(dst_addr).addr;
    p_header->protocol = IP_PROTOCOL_TCP;
    p_header->zero = 0;
    p_header->tcp_length = htons(sizeof(tcp_header));

    t_header->src_port = htons(sock.src_port);
    t_header->dst_port = htons(port);
    t_header->seq_num = 0;
    t_header->ack_num = 0; // todo: 这里要使用时间戳+随机数生成
    t_header->reserved = 0;
    t_header->data_offset = sizeof(tcp_header) / 4;
    t_header->flags = 0;
    t_header->flags = (uint8_t)tcp_flags::SYN;
    t_header->window = htons(tcb->window_size);
    t_header->checksum = 0;
    t_header->urgent_ptr = 0;

    t_header->checksum = checksum(header, sizeof(pseudo_tcp_header) + sizeof(tcp_header));

    map_to_sock[tcp_quadruple {.local_ip = p_header->src_addr, .remote_ip = p_header->dst_addr,
                               .local_port = t_header->src_port, .remote_port = t_header->dst_port}] = &sock;
    if (send_ipv4((ipv4addr(dst_addr)), IP_PROTOCOL_TCP, t_header, sizeof(tcp_header)) != 0) {
        return -1;
    }
    
    int ret = -1;
    uint32_t flags = spinlock_acquire(&(sock.lock));
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
        uint32_t flags = spinlock_acquire(&(sock.lock));
        if (tcb->state == tcb_state::ESTABLISHED) {
            ret = 0;
        }
    }
    spinlock_release(&(sock.lock), flags);
    kfree(header);
    return ret;
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
        printf("discard.");
        return;
    }
    SpinlockGuard guard(itr->second->lock);
    TCB* tcb = (TCB*)itr->second->data;
    // todo: 把数据写入缓冲区
}