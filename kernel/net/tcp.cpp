#include <kernel/net/tcp.hpp>
#include <kernel/mm.h>
#include <kernel/process.hpp>
#include <kernel/timer.hpp>
#include <kernel/schedule.h>
#include <format.h>
#include <unordered_map>

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

struct sockaddr_hasher {
    size_t operator()(const sockaddr& q) const {
        size_t seed = 0;
        auto combine = [&](uint32_t v) {
            // 经典的位扰动算法，防止哈希冲突
            seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };
        combine(q.addr);
        combine(q.port);
        return seed;
    }
};

spinlock map_sockaddr_lock;
spinlock map_quad_lock;
std::unordered_map<sockaddr, TCB*, sockaddr_hasher> map_sockaddr_to_sock;
std::unordered_map<tcp_quadruple, TCB*, tcp_hasher> map_quad_to_sock;

constexpr uint32_t DEFAULT_WINDOW_SIZE = (1 << 16) - 1;
constexpr uint32_t DEFAULT_WINDOW_SCALE = 1;

int tcp_init(socket& sock, uint16_t local_port) {
    sock.ptcl = protocol::TCP;

    sock.data.tcp.block = new (kmalloc(sizeof(TCB))) TCB();
    TCB* tcb = sock.data.tcp.block;
    tcb->state = tcb_state::CLOSED;
    tcb->window_size = DEFAULT_WINDOW_SIZE * DEFAULT_WINDOW_SCALE;
    tcb->window = (char*)kmalloc(tcb->window_size);
    tcb->window_head = 0;
    tcb->window_tail = 0;
    tcb->window_used_size = 0;
    tcb->seq = 0; // todo: 这里要使用时间戳+随机数生成
    tcb->ack = 0;
    tcb->accepted_queue_size = 0;
    tcb->pending_count = 0;
    tcb->dst_addr = 0; // 先给一个默认地址，后面通过connect设置
    tcb->dst_port = 0; // 与上面同理

    tcb->src_addr = getLocalNetconf()->ip.addr;
    tcb->src_port = htons(local_port);

    tcb->owner = &sock;
    tcb->listener = nullptr;
    return 0;
}

int send_tcp_pack(TCB* tcb, uint8_t flags, const char* payload, size_t size) {
    void* packet = kmalloc(sizeof(pseudo_tcp_header) + sizeof(tcp_header) + size);
    uint32_t packet_size = sizeof(pseudo_tcp_header) + sizeof(tcp_header) + size;
    memset(packet, 0, packet_size);
    pseudo_tcp_header* p_header = (pseudo_tcp_header*)packet;
    tcp_header* t_header = (tcp_header*)((char*)packet + sizeof(pseudo_tcp_header));

    p_header->src_addr = tcb->src_addr;
    p_header->dst_addr = tcb->dst_addr;
    p_header->protocol = IP_PROTOCOL_TCP;
    p_header->zero = 0;
    p_header->tcp_length = htons(sizeof(tcp_header) + size);

    t_header->src_port = tcb->src_port;
    t_header->dst_port = tcb->dst_port;
    t_header->seq_num = htonl(tcb->seq);
    t_header->ack_num = htonl(tcb->ack);
    t_header->reserved = 0;
    t_header->data_offset = sizeof(tcp_header) / 4;
    t_header->flags = (uint8_t)flags;
    t_header->window = htons(tcb->window_size - tcb->window_used_size);
    t_header->checksum = 0;
    t_header->urgent_ptr = 0;
    memcpy(((char*)packet + sizeof(pseudo_tcp_header) + sizeof(tcp_header)), payload, size);
    t_header->checksum = checksum(packet, packet_size);
    tcb->seq += size;
    // 需要被可靠确认的标志位都需要虚拟字节
    if (((uint8_t)flags & (uint8_t)tcp_flags::SYN) || ((uint8_t)flags & (uint8_t)tcp_flags::FIN)) {
        tcb->seq += 1; // 虚拟字节
    }
    int ret = send_ipv4((ipv4addr(tcb->dst_addr)), IP_PROTOCOL_TCP, t_header, sizeof(tcp_header) + size);
    kfree(packet);
    return ret;
}

int tcp_close(socket& sock) {
    SpinlockGuard guard(sock.lock);
    TCB* tcb = sock.data.tcp.block;
    PCB* cur;
    // 这里我拿sock里面的wait_queue好像没什么办法，因为这说明了进程正在被阻塞，是没法主动调close的
    // 除非是收到了一些外部事件，但是这一般来说，会是整个进程都会被销毁掉的情况
    sock.valid = 0;

    // 连接关闭都发不出去也太衰了，但这里也只有三次机会
    send_tcp_pack(tcb, ((uint8_t)tcp_flags::FIN | (uint8_t)tcp_flags::ACK), nullptr, 0);
    tcb->state = tcb_state::FIN_WAIT1;
    return 0;
}

int tcp_bind(TCB* tcb, sockaddr* bind_conf) {
    tcb->src_addr = bind_conf->addr;
    tcb->src_port = htons(bind_conf->port);
    return 0;
}

int tcp_ioctl(TCB* tcb, const char* cmd, void* arg) {
    if (strcmp(cmd, "SOCK_IOC_BIND") == 0) {
        return tcp_bind(tcb, reinterpret_cast<sockaddr*>(arg));
    }
    return -1;
}

int tcp_connect(socket& sock, uint32_t addr, uint16_t port) {
    TCB* tcb = sock.data.tcp.block;
    uint32_t flags = spinlock_acquire(&(tcb->lock));
    tcb->dst_addr = addr;
    tcb->dst_port = htons(port);
    {
        SpinlockGuard guard(map_quad_lock);
        map_quad_to_sock[tcp_quadruple {.local_ip = tcb->src_addr, .remote_ip = tcb->dst_addr,
                                        .local_port = tcb->src_port, .remote_port = tcb->dst_port}] = tcb;
    }
    // SEND SYN
    if (send_tcp_pack(tcb, (uint8_t)tcp_flags::SYN, nullptr, 0) < 0) {
        spinlock_release(&(tcb->lock), flags);
        return -1;
    }
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
        spinlock_release(&(tcb->lock), flags);
        timeout(&(sock.wait_queue), 3000);
        flags = spinlock_acquire(&(tcb->lock));
        if (tcb->state == tcb_state::ESTABLISHED) {
            ret = 0;
        }
    }
    spinlock_release(&(tcb->lock), flags);
    return ret;
}

int tcp_listen(socket& sock, size_t queue_length) {
    TCB* tcb = sock.data.tcp.block;
    if (tcb->state == tcb_state::LISTEN) {
        return -1;
    }
    tcb->state = tcb_state::LISTEN;
    tcb->accepted_queue_size = queue_length;

    sockaddr config;
    config.addr = tcb->src_addr;
    config.port = tcb->src_port;
    {
        SpinlockGuard guard(map_sockaddr_lock);
        map_sockaddr_to_sock[config] = tcb;
    }
    return 0;
}

static void destroy_tcb(pid_t pid, void* tcb) {
    kfree(reinterpret_cast<TCB*>(tcb)->window);
    kfree(tcb);
}

static void time_wait(TCB* tcb) {
    register_timer(pit_get_ticks() + 300, &destroy_tcb, tcb); // 10ms 1tick, 也就是等3秒
}

// 这个函数比较特殊，因为它会产生一个新的fd。
// 我们按这样的思路去做
// 原本的调用链条是accept()->sys_accept()->v_accept()->sockfs_accept()->tcp_accept()
// 那我们的返回过程就是tcp_accept()返回一个新TCB->sockfs_accept()封装成一个socket，返回inode_id->
// v_accept()封装成一个fd，然后用户就能拿到一个新的fd了
TCB* tcp_accept(socket& sock, sockaddr* peeraddr, size_t* size) {
    TCB* tcb = sock.data.tcp.block;
    uint32_t flags = spinlock_acquire(&(tcb->lock));
    if (tcb->state != tcb_state::LISTEN) {
        spinlock_release(&(tcb->lock), flags);
        return nullptr;
    }
    TCB* ret = nullptr;
    if (!tcb->accepted_queue.empty()) {
        ret = tcb->accepted_queue.front();
        tcb->accepted_queue.pop_into(ret);
    } else {
        {
            SpinlockGuard guard(process_list_lock);
            process_list[cur_process_id]->state = process_state::WAITING;
            insert_into_process_queue(sock.wait_queue, process_list[cur_process_id]);
        }
        spinlock_release(&(tcb->lock), flags);
        yield();
        flags = spinlock_acquire(&(tcb->lock));
        if (!tcb->accepted_queue.empty()) {
            ret = tcb->accepted_queue.front();
            tcb->accepted_queue.pop_into(ret);
        }
    }
    spinlock_release(&(tcb->lock), flags);
    return ret;
}

int tcp_read(socket& sock, char* buffer, uint32_t size) {
    // head 与 tail，左闭右开
    TCB* tcb = sock.data.tcp.block;
    SpinlockGuard guard(tcb->lock);
    char* window = sock.data.tcp.block->window;
    size_t read_size;
    if (tcb->window_used_size == 0) {
        return 0;
    }
    if (tcb->window_head < tcb->window_tail) {
        read_size = tcb->window_tail - tcb->window_head < size?
                           tcb->window_tail - tcb->window_head : size;
        memcpy(buffer, window + tcb->window_head, read_size);
    } else {
        size_t first = tcb->window_size - tcb->window_head;
        if (size <= first) {
            memcpy(buffer, window + tcb->window_head, size);
            read_size = size;
        } else {
            memcpy(buffer, window + tcb->window_head, first);
            size_t second = (size - first) > tcb->window_tail ? tcb->window_tail : size - first;
            memcpy(buffer + first, window, second);
            read_size = first + second;
        }
    }
    tcb->window_head = (tcb->window_head + read_size) % tcb->window_size;
    tcb->window_used_size -= read_size;
    return read_size;
}

int tcp_write(socket& sock, char* buffer, uint32_t size) {
    TCB* tcb = sock.data.tcp.block;
    int ret = send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, buffer, size);
    return ret;
}

void tcp_handler(uint16_t ip_header_size, char* buffer, uint16_t size) {
    tcp_header* header = reinterpret_cast<tcp_header*>(buffer + ip_header_size);
    uint32_t src_ip = reinterpret_cast<ip_header*>(buffer)->src_ip;
    uint32_t dst_ip = reinterpret_cast<ip_header*>(buffer)->dst_ip;
    uint16_t src_port = header->src_port;
    uint16_t dst_port = header->dst_port;

    uint32_t flags = spinlock_acquire(&map_quad_lock);
    auto itr = map_quad_to_sock.find(tcp_quadruple {.local_ip = dst_ip, .remote_ip = src_ip,
                               .local_port = dst_port, .remote_port = src_port});
    if (itr == map_quad_to_sock.end()) { // 没在已有的连接找到
        spinlock_release(&map_quad_lock, flags);
        sockaddr tofind_addr;
        tofind_addr.addr = dst_ip;
        tofind_addr.port = dst_port;
        socket* listener = nullptr;
        {
            SpinlockGuard guard(map_sockaddr_lock);
            auto itr = map_sockaddr_to_sock.find(tofind_addr);
            if (itr == map_sockaddr_to_sock.end()) {
                // 如果按特定ip找没找到，那就找0.0.0.0
                tofind_addr.addr = 0;
                itr = map_sockaddr_to_sock.find(tofind_addr);
                if (itr == map_sockaddr_to_sock.end()) { return; } // 实在找不到对应的，就丢弃掉了
            }
            listener = itr->second->owner;
        }
        if (header->flags != (uint8_t)tcp_flags::SYN) return;
        
        TCB* listener_tcb = listener->data.tcp.block;
        TCB* tcb = new (kmalloc(sizeof(TCB))) TCB();
        
        tcb->state = tcb_state::SYN_RCVD;
        tcb->window_size = DEFAULT_WINDOW_SIZE * DEFAULT_WINDOW_SCALE;
        tcb->window = (char*)kmalloc(tcb->window_size);
        tcb->window_head = 0;
        tcb->window_tail = 0;
        tcb->window_used_size = 0;
        tcb->seq = 0; // todo: 这里要使用时间戳+随机数生成
        tcb->ack = ntohl(header->seq_num) + 1;
        tcb->accepted_queue_size = 0;
        tcb->pending_count = 0;

        tcb->dst_addr = src_ip;
        tcb->dst_port = src_port;
        tcb->src_addr = dst_ip;
        tcb->src_port = dst_port;

        tcb->owner = nullptr;
        tcb->listener = listener;
        {
            SpinlockGuard listener_guard(listener_tcb->lock);
            if (listener_tcb->accepted_queue.size() + listener_tcb->pending_count >=
                listener_tcb->accepted_queue_size) {
                // 接受队列满了，重置连接
                send_tcp_pack(tcb, (uint8_t)tcp_flags::RST, nullptr, 0);
                kfree(tcb->window);
                tcb->~TCB();
                kfree(tcb);
                return;
            }
            listener_tcb->pending_count++;
        }
        if (send_tcp_pack(tcb, ((uint8_t)tcp_flags::SYN | (uint8_t)tcp_flags::ACK), nullptr, 0) < 0) {
            {
                SpinlockGuard listener_guard(listener_tcb->lock);
                listener_tcb->pending_count--;
            }
            kfree(tcb->window);
            tcb->~TCB();
            kfree(tcb);
            return;
        }

        SpinlockGuard quadGuard(map_quad_lock);
        // SYNACK发送成功后，我们就可以把自己放入四元组了
        map_quad_to_sock[tcp_quadruple {.local_ip = tcb->src_addr, .remote_ip = tcb->dst_addr,
                                        .local_port = tcb->src_port, .remote_port = tcb->dst_port}] = tcb;
        return;
    }
    TCB* tcb = itr->second;
    spinlock_release(&map_quad_lock, flags);

    SpinlockGuard guard(tcb->lock);
    socket* sock = tcb->owner;
    
    switch (tcb->state)
    {
    case tcb_state::CLOSED:
        return;
    case tcb_state::SYN_RCVD:
    {
        if (header->flags != (uint8_t)tcp_flags::ACK) return;
        TCB* listener_tcb = tcb->listener->data.tcp.block;
        tcb->state = tcb_state::ESTABLISHED;
        SpinlockGuard listener_guard(listener_tcb->lock);
        tcb->listener->data.tcp.block->accepted_queue.push(tcb);
        listener_tcb->pending_count--;
        {
            SpinlockGuard guard(process_list_lock);
            PCB* cur;
            while(cur = tcb->listener->wait_queue) {
                remove_from_process_queue(tcb->listener->wait_queue, cur->pid);
                cur->state = process_state::READY;
                insert_into_scheduling_queue(cur->pid);
            }
        }
        break;
    }
    case tcb_state::SYN_SENT:
        if ((header->flags & ((uint8_t)tcp_flags::SYN | (uint8_t)tcp_flags::ACK)) != 
            ((uint8_t)tcp_flags::SYN | (uint8_t)tcp_flags::ACK)) {
            break;
        }
        tcb->ack = ntohl(header->seq_num) + 1; // 下一次希望收到的：SYN出来的序列号，加一个虚拟字节
        send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0);
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
    case tcb_state::TIME_WAIT:
        if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) {
            // 这回不用加ACK了，我们之前肯定加过了
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0);
            // 这里不重置定时器，就相当于定时器是限定“尝试发送所有的最后一个ACK”的尝试周期
            // 你也可以重置，这样每次尝试发最后一个ACK都是独立计时的，我这里就简单实现了
        }
        break;
    case tcb_state::FIN_WAIT1:
        if ((header->flags & (uint8_t)tcp_flags::ACK) != 0) {
            tcb->state = tcb_state::FIN_WAIT2; // 确认了我们这边已经发完了东西
        } else if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) { // 如果收到了FIN但是没有ACK，那就是刚好对端也在关闭连接
            tcb->state = tcb_state::CLOSING; // 那就转成CLOSING
            tcb->ack = ntohl(header->seq_num) + 1; // FIN会占一个虚拟字节
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0);
            // 注意这里虽然还是会跑到下面，但是会因为不带ACK，对方的数据被丢弃。
            // 不过如果真是这样，对方在FIN_WAIT1的时候同时发FIN，还不带ACK地把数据发过来，也太不讲规矩了吧。
            // 这种情况下，他的数据丢掉就丢掉了。
        }
        [[fallthrough]];
    case tcb_state::FIN_WAIT2:
        [[fallthrough]];
    case tcb_state::ESTABLISHED:
    {
        if (ntohl(header->seq_num) != tcb->ack) { // 我们先做一个简单实现，乱序的直接丢弃
            break;
        }
        if ((header->flags & (uint8_t)tcp_flags::RST) != 0) {
            // todo: 正确handle RST
            break;
        }
        if ((header->flags & (uint8_t)tcp_flags::ACK) == 0) {
            break;
        }
        size_t payload_size = size - ip_header_size - header->data_offset * 4; // 别忘了乘4
        char* payload = buffer + header->data_offset * 4 + ip_header_size;
        if (tcb->window_size - tcb->window_used_size < payload_size) {
            break; // 超过当前窗口大小直接丢弃
        }
        if (tcb->window_tail < tcb->window_head) {
            memcpy(tcb->window + tcb->window_tail, payload, payload_size);
        } else {
            size_t first = tcb->window_size - tcb->window_tail;
            if (first >= payload_size) {
                memcpy(tcb->window + tcb->window_tail, payload, payload_size);
            } else {
                memcpy(tcb->window + tcb->window_tail, payload, first);
                size_t second = payload_size - first;
                memcpy(tcb->window, payload + first, second);
            }
        }
        tcb->window_used_size += payload_size;
        tcb->window_tail = (tcb->window_tail + payload_size) % tcb->window_size;
        tcb->ack += payload_size;
        send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0);
        { // 阻塞式read
            SpinlockGuard guard(process_list_lock);
            PCB* cur;
            while(cur = sock->wait_queue) {
                remove_from_process_queue(sock->wait_queue, cur->pid);
                cur->state = process_state::READY;
                insert_into_scheduling_queue(cur->pid);
            }
        }
        { // poll
            SpinlockGuard guard(process_list_lock);
            PCB* cur;
            while((sock->poll_queue != nullptr) && (*(sock->poll_queue) != nullptr) &&
                (cur = *(sock->poll_queue))) {
                remove_from_process_queue(*(sock->poll_queue), cur->pid);
                cur->state = process_state::READY;
                insert_into_scheduling_queue(cur->pid);
            }
        }
        if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) {
            tcb->ack = ntohl(header->seq_num) + 1;
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0); // 好
            if (tcb->state == tcb_state::FIN_WAIT2) { // 如果我们实际上是FIN_WAIT2
                tcb->state = tcb_state::TIME_WAIT;
                time_wait(tcb);
                break;
            } else {
                printf("passively closed!\n");
            }
        }
        break;
    }
    default:
        break;
    }
}