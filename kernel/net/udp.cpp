#include <kernel/net/udp.hpp>
#include <kernel/mm.h>
#include <kernel/process.hpp>
#include <kernel/timer.hpp>
#include <kernel/schedule.h>
#include <format.h>
#include <unordered_map>

constexpr uint32_t UDP_BUFFER_SIZE = 4096;

spinlock map_sockaddr_lock;
spinlock map_quad_lock;
std::unordered_map<sockaddr, socket*, sockaddr_hasher> map_sockaddr_to_sock;
std::unordered_map<conn_quadruple, socket*, conn_hasher> map_quad_to_sock;

// 清理已有的二元组记录
// 这里只要local_port是用端口池分配的就不会把别人的给清掉
// 现在还是用的临时方案，固定端口，因此建议用udp一上来都手动bind一下
void udp_clear_my_sockaddr(socket& sock) {
    sockaddr cur_conf = {.addr = sock.data.udp.local_ip,
                         .port = sock.data.udp.local_port};
    map_sockaddr_to_sock.erase(cur_conf);
}

// 清理已有的四元组记录
void udp_clear_my_quad(socket& sock) {
    conn_quadruple current_conn = {.local_ip = sock.data.udp.local_ip,
                                   .remote_ip = sock.data.udp.remote_ip,
                                   .local_port = sock.data.udp.local_port,
                                   .remote_port = sock.data.udp.remote_port};
    auto itr = map_quad_to_sock.find(current_conn);
    if (itr != map_quad_to_sock.end() && itr->second == &sock) { // 已有的四元组是我的记录
        map_quad_to_sock.erase(current_conn);
    } 
}

int udp_bind(socket& sock, sockaddr* bind_conf) {
    SpinlockGuard sockGuard(sock.lock);
    {
        SpinlockGuard sockGuard(map_sockaddr_lock);
        // 检查已有的二元组记录，不要把别人的记录给占了
        auto itr = map_sockaddr_to_sock.find(*bind_conf);
        if (itr != map_sockaddr_to_sock.end() && itr->second != &sock) {
            return -1; // 该本地端口、地址组合已被绑定，且不是我自己
        }
        udp_clear_my_sockaddr(sock);
    }
    {
        // bind成功后，原来的四元组就失效了，要清掉
        SpinlockGuard sockGuard(map_quad_lock);
        udp_clear_my_quad(sock);
    }
    // 到这里才开始插入map和更新本地ip或端口
    sock.data.udp.local_ip = bind_conf->addr;
    sock.data.udp.local_port = bind_conf->port;
    {
        SpinlockGuard sockGuard(map_sockaddr_lock);
        map_sockaddr_to_sock[*bind_conf] = &sock;
    }
    return 0;
}

int udp_init(socket& sock, uint16_t local_port) {
    sock.ptcl = protocol::UDP;
    sock.data.udp.pack_head = nullptr;
    sock.data.udp.pack_tail = nullptr;
    sockaddr init_setting = {.addr = 0, .port = htons(local_port)};
    return udp_bind(sock, &init_setting); // 先绑定一个本地端口，接收广播
}

int udp_close(socket& sock) {
    SpinlockGuard sockGuard(sock.lock);
    {
        SpinlockGuard sockGuard(map_sockaddr_lock);
        udp_clear_my_sockaddr(sock);
    }
    {
        SpinlockGuard sockGuard(map_quad_lock);
        udp_clear_my_quad(sock);
    }
    udp_pack* cur = sock.data.udp.pack_head;
    while(cur) {
        kfree(cur->data);
        udp_pack* next = cur->next;
        kfree(cur);
        cur = next;
    }
    sock.data.udp.pack_head = nullptr;
    sock.data.udp.pack_tail = nullptr;
    wake_all_queue(&sock);
    sock.valid = 0;
    return 0;
}

int udp_ioctl(socket& sock, const char* cmd, void* arg) {
    if (strcmp(cmd, "SOCK_IOC_BIND") == 0) {
        return udp_bind(sock, reinterpret_cast<sockaddr*>(arg));
    }
    return -1;
}

int udp_connect(socket& sock, uint32_t addr, uint16_t port) {
    SpinlockGuard sockGuard(sock.lock);
    // connect需要先检查当前connect的记录是否已经有人占用
    conn_quadruple current_conn = {.local_ip = sock.data.udp.local_ip,
                                   .remote_ip = htonl(addr),
                                   .local_port = sock.data.udp.local_port,
                                   .remote_port = htons(port)};
    {
        SpinlockGuard guard(map_quad_lock);
        auto itr = map_quad_to_sock.find(current_conn);
        if (itr != map_quad_to_sock.end() && itr->second != &sock) { // 当前四元组已被占用且不是我自己
            return -1;
        }
        udp_clear_my_quad(sock);
        sock.data.udp.remote_ip = htonl(addr);
        sock.data.udp.remote_port = htons(port);
        map_quad_to_sock[current_conn] = &sock;
    }
    return 0;
}

int udp_sendto(socket& sock, const char* buffer, uint32_t size, sockaddr* peeraddr) {
    uint32_t dst_ip;
    uint16_t dst_port;

    if (peeraddr) {
        dst_ip = peeraddr->addr;
        dst_port = peeraddr->port;
    } else {
        SpinlockGuard guard(sock.lock);
        if (sock.data.udp.remote_ip == 0 && sock.data.udp.remote_port == 0)
            return -1;
        dst_ip = sock.data.udp.remote_ip;
        dst_port = sock.data.udp.remote_port;
    }

    uint32_t packet_size = sizeof(pseudo_ip_header) + sizeof(udp_header) + size;
    void* packet = kmalloc(packet_size);
    memset(packet, 0, packet_size);

    pseudo_ip_header* p_header = (pseudo_ip_header*)packet;
    udp_header* t_header = (udp_header*)((char*)packet + sizeof(pseudo_ip_header));

    p_header->src_addr = (sock.data.udp.local_ip == SOCKADDR_BROADCAST_ADDR) ? getLocalNetconf()->ip.addr :
                          sock.data.udp.local_ip;
    p_header->dst_addr = dst_ip;
    p_header->protocol = IP_PROTOCOL_UDP;
    p_header->zero = 0;
    p_header->data_length = htons(sizeof(udp_header) + size);

    t_header->src_port = sock.data.udp.local_port;
    t_header->dst_port = dst_port;
    t_header->size = htons(sizeof(udp_header) + size);
    t_header->checksum = 0;
    memcpy((char*)packet + sizeof(pseudo_ip_header) + sizeof(udp_header), buffer, size);
    t_header->checksum = checksum(packet, packet_size);

    int ret = send_ipv4(ipv4addr(dst_ip), IP_PROTOCOL_UDP,
                        t_header, sizeof(udp_header) + size);
    kfree(packet);
    return ret;
}

int udp_recvfrom(socket& sock, char* buffer, uint32_t size, sockaddr* peeraddr) {
    int ret = -1;
    uint32_t flags = spinlock_acquire(&(sock.lock));

    if (sock.data.udp.pack_head) {
        udp_pack* head = sock.data.udp.pack_head;
        size_t loadsize = size < head->size ? size : head->size;
        memcpy(buffer, head->data, loadsize);
        if (peeraddr) {
            peeraddr->addr = head->remote_ip;
            peeraddr->port = head->remote_port;
        }
        kfree(head->data);
        sock.data.udp.pack_head = head->next;
        if (sock.data.udp.pack_head == nullptr)
            sock.data.udp.pack_tail = nullptr;
        kfree(head);
        ret = loadsize;
    } else {
        {
            SpinlockGuard guard(process_list_lock);
            process_list[cur_process_id]->state = process_state::WAITING;
            insert_into_waiting_queue(sock.wait_queue, process_list[cur_process_id]);
        }
        spinlock_release(&(sock.lock), flags);
        timeout(&(sock.wait_queue), 3000);
        flags = spinlock_acquire(&(sock.lock));

        if (sock.data.udp.pack_head) {
            udp_pack* head = sock.data.udp.pack_head;
            size_t loadsize = size < head->size ? size : head->size;
            memcpy(buffer, head->data, loadsize);
            if (peeraddr) {
                peeraddr->addr = head->remote_ip;
                peeraddr->port = head->remote_port;
            }
            kfree(head->data);
            sock.data.udp.pack_head = head->next;
            if (sock.data.udp.pack_head == nullptr)
                sock.data.udp.pack_tail = nullptr;
            kfree(head);
            ret = loadsize;
        } else {
            ret = -2; // timed out
        }
    }

    spinlock_release(&(sock.lock), flags);
    return ret;
}

int udp_read(socket& sock, char* buffer, uint32_t size) { // recv
    int ret = -1;
    uint32_t flags = spinlock_acquire(&(sock.lock));
    if (sock.data.udp.pack_head) {
        size_t loadsize = size < sock.data.udp.pack_head->size ? size :
            sock.data.udp.pack_head->size;
        memcpy(buffer, sock.data.udp.pack_head->data, loadsize);
        kfree(sock.data.udp.pack_head->data);
        udp_pack* next = sock.data.udp.pack_head->next;
        kfree(sock.data.udp.pack_head);
        sock.data.udp.pack_head = next;
        if (sock.data.udp.pack_head == nullptr) {
            sock.data.udp.pack_tail = nullptr;
        }
        ret = loadsize;
    } else {
        {
            SpinlockGuard guard(process_list_lock);
            process_list[cur_process_id]->state = process_state::WAITING;
            insert_into_waiting_queue(sock.wait_queue, process_list[cur_process_id]);
        }
        spinlock_release(&(sock.lock), flags);
        timeout(&(sock.wait_queue), 3000);
        flags = spinlock_acquire(&(sock.lock));
        if (sock.data.udp.pack_head) {
            size_t loadsize = size < sock.data.udp.pack_head->size ? size :
                sock.data.udp.pack_head->size;
            memcpy(buffer, sock.data.udp.pack_head->data, loadsize);
            kfree(sock.data.udp.pack_head->data);
            udp_pack* next = sock.data.udp.pack_head->next;
            kfree(sock.data.udp.pack_head);
            sock.data.udp.pack_head = next;
            if (sock.data.udp.pack_head == nullptr) {
                sock.data.udp.pack_tail = nullptr;
            }
            ret = loadsize;
        } else {
            ret = -2; // timed out
        }
    }
    spinlock_release(&(sock.lock), flags);
    return ret;

}

int udp_write(socket& sock, const char* payload, uint32_t size) {
    void* packet = kmalloc(sizeof(pseudo_ip_header) + sizeof(udp_header) + size);
    uint32_t packet_size = sizeof(pseudo_ip_header) + sizeof(udp_header) + size;
    memset(packet, 0, packet_size);
    pseudo_ip_header* p_header = (pseudo_ip_header*)packet;
    udp_header* t_header = (udp_header*)((char*)packet + sizeof(pseudo_ip_header));

    p_header->src_addr = (sock.data.udp.local_ip == SOCKADDR_BROADCAST_ADDR) ? getLocalNetconf()->ip.addr :
                          sock.data.udp.local_ip;
    p_header->dst_addr = sock.data.udp.remote_ip;
    p_header->protocol = IP_PROTOCOL_UDP;
    p_header->zero = 0;
    p_header->data_length = htons(sizeof(udp_header) + size);

    t_header->src_port = sock.data.udp.local_port;
    t_header->dst_port = sock.data.udp.remote_port;
    t_header->size = htons(sizeof(udp_header) + size);
    t_header->checksum = 0;
    memcpy(((char*)packet + sizeof(pseudo_ip_header) + sizeof(udp_header)), payload, size);
    t_header->checksum = checksum(packet, packet_size);
    int ret = send_ipv4((ipv4addr(sock.data.udp.remote_ip)), IP_PROTOCOL_UDP, t_header, sizeof(udp_header) + size);
    kfree(packet);
    return ret;
}

void udp_handler(uint16_t ip_header_size, char* buffer, uint16_t size) {
    udp_header* header = reinterpret_cast<udp_header*>(buffer + ip_header_size);
    uint32_t src_ip = reinterpret_cast<ip_header*>(buffer)->src_ip;
    uint32_t dst_ip = reinterpret_cast<ip_header*>(buffer)->dst_ip;
    uint16_t src_port = header->src_port;
    uint16_t dst_port = header->dst_port;
    uint16_t payload_size = ntohs(header->size) - sizeof(udp_header);
    uint32_t flags = spinlock_acquire(&map_quad_lock);
    auto itr = map_quad_to_sock.find(conn_quadruple {.local_ip = dst_ip, .remote_ip = src_ip,
                               .local_port = dst_port, .remote_port = src_port});
    socket* target_sock = nullptr;
    if (itr == map_quad_to_sock.end()) { // 没在已有的连接找到
        spinlock_release(&map_quad_lock, flags);
        sockaddr tofind_addr;
        tofind_addr.addr = dst_ip;
        tofind_addr.port = dst_port;
        {
            SpinlockGuard guard(map_sockaddr_lock);
            auto itr = map_sockaddr_to_sock.find(tofind_addr);
            if (itr == map_sockaddr_to_sock.end()) {
                // 如果按特定ip找没找到，那就找0.0.0.0
                tofind_addr.addr = 0;
                itr = map_sockaddr_to_sock.find(tofind_addr);
                if (itr == map_sockaddr_to_sock.end()) { return; } // 实在找不到对应的，就丢弃掉了
            }
            target_sock = itr->second;
        }
    } else {
        target_sock = itr->second;
    }
    spinlock_release(&map_quad_lock, flags);

    if (!target_sock) return; // 不应该发生
    SpinlockGuard guard(target_sock->lock);
    udp_pack* cur = (udp_pack*)kmalloc(sizeof(udp_pack));
    cur->next = nullptr;
    cur->size = payload_size;
    cur->data = kmalloc(payload_size);
    cur->remote_ip = src_ip;
    cur->remote_port = src_port;
    memcpy(cur->data, buffer + ip_header_size + sizeof(udp_header), payload_size);
    if (target_sock->data.udp.pack_tail) {
        target_sock->data.udp.pack_tail->next = cur;
        target_sock->data.udp.pack_tail = cur;
    } else {
        target_sock->data.udp.pack_head = cur;
        target_sock->data.udp.pack_tail = cur;
    }
    wake_all_queue(target_sock);
}