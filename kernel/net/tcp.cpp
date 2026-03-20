#include <kernel/net/tcp.hpp>
#include <kernel/mm.h>
#include <kernel/process.hpp>
#include <kernel/timer.hpp>
#include <kernel/schedule.h>
#include <driver/procfs.hpp>
#include <format.h>
#include <unordered_map>

static spinlock map_sockaddr_lock;
static spinlock map_quad_lock;
static std::unordered_map<sockaddr, TCBPtr, sockaddr_hasher> map_sockaddr_to_sock;
static std::unordered_map<conn_quadruple, TCBPtr, conn_hasher> map_quad_to_sock;

constexpr uint32_t DEFAULT_WINDOW_SIZE = (1 << 16) - 1;
constexpr uint32_t DEFAULT_WINDOW_SCALE = 1;

static mounting_point* tcb_mp = nullptr;

const char* tcb_states[11] = {
    "CLOSED", "SYN_SENT", "ESTABLISHED", "LISTEN", "SYN_RCVD", "FIN_WAIT1", "FIN_WAIT2", "TIME_WAIT", "CLOSING", "CLOSE_WAIT", "LAST_ACK"
};

void set_tcb_mp(mounting_point* mp) {
    tcb_mp = mp;
    uint32_t net_folder_inode_id = register_info_in_procfs(tcb_mp, "/", "net", nullptr, true, nullptr);
    uint32_t net_tcp_folder_inode_id = register_info_in_procfs(tcb_mp, "/net/", "tcp", nullptr, true, nullptr);
}

void delete_procfs_inode(TCB* tcb) {
    delete_from_procfs(tcb_mp, tcb->procfs_inode_id);
}

static int tcb_read(char* buffer, uint32_t offset, uint32_t size, void* arg) {
    char info[1024];
    size_t signed_offset = (size_t)offset;
    if (arg == nullptr) return -1;
    TCB* tcb = (TCB*)arg;

    char state[20];
    uint32_t src_addr, dst_addr;
    uint16_t src_port, dst_port;
    {
        SpinlockGuard guard(tcb->lock);
        strcpy(state, tcb_states[(uint32_t)tcb->state]);
        src_addr = tcb->src_addr;
        dst_addr = tcb->dst_addr;
        src_port = tcb->src_port;
        dst_port = tcb->dst_port;
    }
    memset(info, 0, sizeof(info));
    snprintf(info, sizeof(info),
        "local: %d.%d.%d.%d:%d\n"
        "remote: %d.%d.%d.%d:%d\n"
        "state: %s\n",
        src_addr & 0xFF, (src_addr >> 8) & 0xFF,
        (src_addr >> 16) & 0xFF, (src_addr >> 24) & 0xFF,
        src_port,
        dst_addr & 0xFF, (dst_addr >> 8) & 0xFF,
        (dst_addr >> 16) & 0xFF, (dst_addr >> 24) & 0xFF,
        dst_port,
        state);

    uint32_t len = strlen(info);
    if (signed_offset >= len) return 0;
    uint32_t to_copy = size < len - signed_offset ? size : len - signed_offset;
    strncpy(buffer, info + signed_offset, to_copy);
    return to_copy;
}

uint32_t reg_in_procfs(shared_ptr<TCB>& tcb) {
    char tcb_name[20];
    snprintf(tcb_name, 20, "%x", (unsigned)tcb.get());
    proc_operation* opr = (proc_operation*)kmalloc(sizeof(proc_operation));
    opr->read = &tcb_read;
    uint32_t tcb_inode_id = register_info_in_procfs(tcb_mp, "/net/tcp/", tcb_name, opr, false, tcb.get());
    return tcb_inode_id;
}

int tcp_init(socket& sock, uint16_t local_port) {
    sock.ptcl = protocol::TCP;

    TCBPtr tcb = make_shared<TCB>();
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
    tcb->procfs_inode_id = reg_in_procfs(tcb);
    sock.data.tcp.block = tcb;
    
    return 0;
}

// 定时器回调：销毁堆上的shared_ptr副本，触发引用计数递减
static void destroy_tcb(pid_t, void* arg) {
    TCBPtr* p = reinterpret_cast<TCBPtr*>(arg);
    {
        TCBPtr tcb = static_cast<TCBPtr&&>(*p); // 移动出来
        {
            SpinlockGuard guard(map_quad_lock);
            map_quad_to_sock.erase(conn_quadruple{
                tcb->src_addr, tcb->dst_addr, tcb->src_port, tcb->dst_port});
        }
        // tcb在此作用域结束时析构，若为最后一个引用则通过~TCB()释放window，再kfree(TCB)
    }
    p->~shared_ptr();
    kfree(p);
}

static void time_wait(TCBPtr tcb) {
    // 在堆上创建一个shared_ptr副本，延长生命周期直到定时器触发
    void* mem = kmalloc(sizeof(TCBPtr));
    TCBPtr* p = new (mem) TCBPtr(tcb);
    register_timer(pit_get_ticks() + 300, &destroy_tcb, p); // 10ms 1tick, 也就是等3秒
}

int send_tcp_pack(TCBPtr tcb, uint8_t flags, const char* payload, size_t size) {
    void* packet = kmalloc(sizeof(pseudo_ip_header) + sizeof(tcp_header) + size);
    uint32_t packet_size = sizeof(pseudo_ip_header) + sizeof(tcp_header) + size;
    memset(packet, 0, packet_size);
    pseudo_ip_header* p_header = (pseudo_ip_header*)packet;
    tcp_header* t_header = (tcp_header*)((char*)packet + sizeof(pseudo_ip_header));

    p_header->src_addr = tcb->src_addr;
    p_header->dst_addr = tcb->dst_addr;
    p_header->protocol = IP_PROTOCOL_TCP;
    p_header->zero = 0;
    p_header->data_length = htons(sizeof(tcp_header) + size);

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
    memcpy(((char*)packet + sizeof(pseudo_ip_header) + sizeof(tcp_header)), payload, size);
    t_header->checksum = checksum(packet, packet_size);
    int ret = send_ipv4((ipv4addr(tcb->dst_addr)), IP_PROTOCOL_TCP, t_header, sizeof(tcp_header) + size);
    if (ret >= 0) {
        tcb->seq += size;
        // 需要被可靠确认的标志位都需要虚拟字节
        if (((uint8_t)flags & (uint8_t)tcp_flags::SYN) || ((uint8_t)flags & (uint8_t)tcp_flags::FIN)) {
            tcb->seq += 1; // 虚拟字节
        }
    }
    kfree(packet);
    return ret;
}

int tcp_close(socket& sock) {
    TCBPtr tcb = sock.data.tcp.block;
    PCB* cur;
    // 这里我拿sock里面的wait_queue好像没什么办法，因为这说明了进程正在被阻塞，是没法主动调close的
    // 除非是收到了一些外部事件，但是这一般来说，会是整个进程都会被销毁掉的情况
    sock.valid = 0;
    if (!tcb) return 0;
    SpinlockGuard guard(tcb->lock);
    if (tcb->state == tcb_state::LISTEN) {
        sockaddr config;
        config.addr = tcb->src_addr;
        config.port = tcb->src_port;
        {
            SpinlockGuard guard(map_sockaddr_lock);
            map_sockaddr_to_sock.erase(config);
        }

        // 清理accepted_queue中已ESTABLISHED但未被accept取走的子连接
        while (!tcb->accepted_queue.empty()) {
            TCBPtr child = tcb->accepted_queue.front();
            tcb->accepted_queue.pop_into(child);
            send_tcp_pack(child, (uint8_t)tcp_flags::RST, nullptr, 0);
            {
                SpinlockGuard quad_guard(map_quad_lock);
                map_quad_to_sock.erase(conn_quadruple{
                    child->src_addr, child->dst_addr,
                    child->src_port, child->dst_port});
            }
            // child的shared_ptr在此作用域结束时自动释放（~TCB()会kfree(window)）
        }


        // 清理处于SYN_RCVD的半连接
        {
            SpinlockGuard quad_guard(map_quad_lock);
            auto it = map_quad_to_sock.begin();
            while (it != map_quad_to_sock.end()) {
                TCBPtr child = it->second;
                if (child->listener == &sock &&
                    child->state == tcb_state::SYN_RCVD) {
                    it = map_quad_to_sock.erase(it);
                    send_tcp_pack(child, (uint8_t)tcp_flags::RST, nullptr, 0);
                    // child的shared_ptr在此作用域结束时自动释放
                } else {
                    ++it;
                }
            }
        }
    }
    // 连接关闭都发不出去也太衰了，但这里也只有三次机会
    if (tcb->state == tcb_state::ESTABLISHED) {
        send_tcp_pack(tcb, ((uint8_t)tcp_flags::FIN | (uint8_t)tcp_flags::ACK), nullptr, 0);
        tcb->state = tcb_state::FIN_WAIT1;
    } else if (tcb->state == tcb_state::CLOSE_WAIT) {
        send_tcp_pack(tcb, ((uint8_t)tcp_flags::FIN | (uint8_t)tcp_flags::ACK), nullptr, 0);
        tcb->state = tcb_state::LAST_ACK;
    } else {
        time_wait(tcb);
    }
    return 0;
}

int tcp_bind(TCBPtr tcb, sockaddr* bind_conf) {
    tcb->src_addr = bind_conf->addr;
    tcb->src_port = htons(bind_conf->port);
    return 0;
}

int tcp_ioctl(TCBPtr& tcb, uint32_t request, void* arg) {
    if (request == SOCK_IOC_BIND) {
        return tcp_bind(tcb, reinterpret_cast<sockaddr*>(arg));
    }
    return -1;
}

int tcp_connect(socket& sock, uint32_t addr, uint16_t port) {
    TCBPtr tcb = sock.data.tcp.block;
    uint32_t flags = spinlock_acquire(&(tcb->lock));
    tcb->src_addr = (tcb->src_addr == SOCKADDR_BROADCAST_ADDR) ? getLocalNetconf()->ip.addr :
                     tcb->src_addr;
    tcb->dst_addr = addr;
    tcb->dst_port = htons(port);
    {
        SpinlockGuard guard(map_quad_lock);
        map_quad_to_sock[conn_quadruple {.local_ip = tcb->src_addr, .remote_ip = tcb->dst_addr,
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
            insert_into_waiting_queue(sock.wait_queue, process_list[cur_process_id]);
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
    TCBPtr tcb = sock.data.tcp.block;
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

// 这个函数比较特殊，因为它会产生一个新的fd。
// 我们按这样的思路去做
// 原本的调用链条是accept()->sys_accept()->v_accept()->sockfs_accept()->tcp_accept()
// 那我们的返回过程就是tcp_accept()返回一个新TCB->sockfs_accept()封装成一个socket，返回inode_id->
// v_accept()封装成一个fd，然后用户就能拿到一个新的fd了
TCBPtr tcp_accept(socket& sock, sockaddr* peeraddr, size_t* size) {
    TCBPtr tcb = sock.data.tcp.block;
    uint32_t flags = spinlock_acquire(&(tcb->lock));
    if (tcb->state != tcb_state::LISTEN) {
        spinlock_release(&(tcb->lock), flags);
        return nullptr;
    }
    TCBPtr ret = nullptr;
    if (!tcb->accepted_queue.empty()) {
        ret = tcb->accepted_queue.front();
        tcb->accepted_queue.pop_into(ret);
    } else {
        {
            SpinlockGuard guard(process_list_lock);
            process_list[cur_process_id]->state = process_state::WAITING;
            insert_into_waiting_queue(sock.wait_queue, process_list[cur_process_id]);
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
    TCBPtr tcb = sock.data.tcp.block;
    SpinlockGuard guard(tcb->lock);
    char* window = tcb->window;
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
    TCBPtr tcb = sock.data.tcp.block;
    int ret = send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, buffer, size);
    return ret;
}

void wake_all_queue(socket* sock){
    if (!sock) return;
    { // 阻塞式read
        SpinlockGuard guard(process_list_lock);
        PCB* cur;
        while(cur = sock->wait_queue) {
            remove_from_waiting_queue(sock->wait_queue, cur->pid);
            cur->state = process_state::READY;
            insert_into_scheduling_queue(cur->pid);
        }
    }
    { // poll
        SpinlockGuard guard(process_list_lock);
        PCB* cur;
        while((sock->poll_queue != nullptr) && (*(sock->poll_queue) != nullptr) &&
            (cur = *(sock->poll_queue))) {
            remove_from_waiting_queue(*(sock->poll_queue), cur->pid);
            cur->state = process_state::READY;
            insert_into_scheduling_queue(cur->pid);
        }
    }
}

void tcp_handler(uint16_t ip_header_size, char* buffer, uint16_t size) {
    tcp_header* header = reinterpret_cast<tcp_header*>(buffer + ip_header_size);
    uint32_t src_ip = reinterpret_cast<ip_header*>(buffer)->src_ip;
    uint32_t dst_ip = reinterpret_cast<ip_header*>(buffer)->dst_ip;
    uint16_t src_port = header->src_port;
    uint16_t dst_port = header->dst_port;

    uint32_t flags = spinlock_acquire(&map_quad_lock);
    auto itr = map_quad_to_sock.find(conn_quadruple {.local_ip = dst_ip, .remote_ip = src_ip,
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
        
        TCBPtr listener_tcb = listener->data.tcp.block;
        TCBPtr tcb = make_shared<TCB>();
        
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
        tcb->procfs_inode_id = reg_in_procfs(tcb);
        {
            SpinlockGuard listener_guard(listener_tcb->lock);
            if (listener_tcb->accepted_queue.size() + listener_tcb->pending_count >=
                listener_tcb->accepted_queue_size) {
                // 接受队列满了，重置连接
                send_tcp_pack(tcb, (uint8_t)tcp_flags::RST, nullptr, 0);
                // tcb的shared_ptr在此作用域结束时自动释放（~TCB()会kfree(window)）
                return;
            }
            listener_tcb->pending_count++;
        }
        if (send_tcp_pack(tcb, ((uint8_t)tcp_flags::SYN | (uint8_t)tcp_flags::ACK), nullptr, 0) < 0) {
            {
                SpinlockGuard listener_guard(listener_tcb->lock);
                listener_tcb->pending_count--;
            }
            // tcb的shared_ptr在此作用域结束时自动释放
            return;
        }

        SpinlockGuard quadGuard(map_quad_lock);
        // SYNACK发送成功后，我们就可以把自己放入四元组了
        map_quad_to_sock[conn_quadruple {.local_ip = tcb->src_addr, .remote_ip = tcb->dst_addr,
                                        .local_port = tcb->src_port, .remote_port = tcb->dst_port}] = tcb;
        return;
    }
    TCBPtr tcb = itr->second;
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
        TCBPtr listener_tcb = tcb->listener->data.tcp.block;
        tcb->state = tcb_state::ESTABLISHED;
        SpinlockGuard listener_guard(listener_tcb->lock);
        listener_tcb->accepted_queue.push(tcb);
        listener_tcb->pending_count--;
        {
            SpinlockGuard guard(process_list_lock);
            PCB* cur;
            while(cur = tcb->listener->wait_queue) {
                remove_from_waiting_queue(tcb->listener->wait_queue, cur->pid);
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
                remove_from_waiting_queue(sock->wait_queue, cur->pid);
                cur->state = process_state::READY;
                insert_into_scheduling_queue(cur->pid);
            }
        }
        break;
    case tcb_state::TIME_WAIT:
        if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) {
            // 这回不用加ACK了，我们之前肯定加过了
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0);
            // 这里不重置定时器，就相当于定时器是限定"尝试发送所有的最后一个ACK"的尝试周期
            // 你也可以重置，这样每次尝试发最后一个ACK都是独立计时的，我这里就简单实现了
        }
        break;
    case tcb_state::LAST_ACK:
        if ((header->flags & (uint8_t)tcp_flags::ACK) != 0) { // 收到最后一个ACK
            time_wait(tcb); // 我们还拿着锁，不能马上释放
        }
        break;
    case tcb_state::CLOSING:
        if ((header->flags & (uint8_t)tcp_flags::ACK) != 0) {
            // 我们之前的FIN的ACK收到了，所以，对方知道了我们不发数据了
            // 但是我们之前发出去的ACK，对方有没有收到呢？如果没有收到，对方会再发一次FIN
            // 所以我们要进入TIME_WAIT
            tcb->state = tcb_state::TIME_WAIT;
            time_wait(tcb);
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
        wake_all_queue(sock);
        if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) { // 对端通知，后面不发数据了
            tcb->ack += 1;
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0); // 好
            if (tcb->state == tcb_state::FIN_WAIT2) { // 如果我们实际上是FIN_WAIT2
                tcb->state = tcb_state::TIME_WAIT;
                time_wait(tcb);
            } else { // 被动关闭的情况
                tcb->state = tcb_state::CLOSE_WAIT;
                wake_all_queue(sock);
            }
        }
        break;
    }
    default:
        break;
    }
}