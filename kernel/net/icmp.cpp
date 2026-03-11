#include <kernel/net/icmp.hpp>
#include <kernel/net/ip.hpp>
#include <kernel/net/net.hpp>
#include <kernel/net/socket.hpp>
#include <driver/sockfs.hpp>
#include <kernel/mm.hpp>
#include <kernel/process.hpp>
#include <kernel/timer.hpp>
#include <format.h>
#include <stdio.h>

int icmp_init(socket& sock) {
    sock.ptcl = protocol::ICMP;
    return 0;
}
int icmp_write(socket& sock, char* buffer, uint32_t size) {
    // 虽然我们现在的socket数量最大是1024，在这个情况下这个标识符应该是但是我不排除后面会有调大这个数量的情况
    // 我不希望调大socket数的时候忘掉了修改这里导致标识符冲突，因此我需要在这里加个断言...
    // 虽然现在这个断言永不失败，但谁知道呢？或许后面我会把MAX_SOCK_NUM换个类型。
    static_assert(MAX_SOCK_NUM <= 65536);
    *reinterpret_cast<uint16_t*>(buffer + 4) = (uint16_t)sock.inode_id;
    *reinterpret_cast<uint16_t*>(buffer + 2) = 0;
    uint16_t chksum = checksum(buffer, size);
    *reinterpret_cast<uint16_t*>(buffer + 2) = chksum;
    int ret = send_ipv4(ipv4addr(sock.data.icmp.bound_ip), IP_PROTOCOL_ICMP, buffer, size);
    return ret;
}
int icmp_read(socket& sock, char* buffer, uint32_t size) {
    int ret = -1;
    {
        uint32_t flags = spinlock_acquire(&(sock.lock));
        if (sock.data.icmp.queue_head) {
            icmp_node* icmp_data = sock.data.icmp.queue_head;
            size_t cpysize = icmp_data->size < size ? icmp_data->size : size;
            memcpy(buffer, icmp_data->data, cpysize);
            icmp_node* next = icmp_data->next;
            kfree(icmp_data->data);
            kfree(sock.data.icmp.queue_head);
            sock.data.icmp.queue_head = next;
            ret = cpysize;
        } else {
            {
                SpinlockGuard guard(process_list_lock);
                process_list[cur_process_id]->state = process_state::WAITING;
                insert_into_process_queue(sock.wait_queue, process_list[cur_process_id]);
            }
            spinlock_release(&(sock.lock), flags);
            timeout(&(sock.wait_queue), 1000);
            flags = spinlock_acquire(&(sock.lock));
            if (sock.data.icmp.queue_head) {
                icmp_node* icmp_data = (icmp_node*)sock.data.icmp.queue_head;
                size_t cpysize = icmp_data->size < size ? icmp_data->size : size;
                memcpy(buffer, icmp_data->data, cpysize);
                icmp_node* next = icmp_data->next;
                kfree(icmp_data->data);
                kfree(sock.data.icmp.queue_head);
                sock.data.icmp.queue_head = next;
                ret = cpysize;
            }
        }
        spinlock_release(&(sock.lock), flags);
    }

    return ret;
}

int icmp_connect(socket& sock, uint32_t addr, uint16_t) {
    sock.data.icmp.bound_ip = addr;
    return 0;
}

void handle_echo_request(const ipv4addr& src_ip, char* buffer, uint16_t size) {
    char* cp_buf = (char*)kmalloc(size);
    memcpy(cp_buf, buffer, size);
    *reinterpret_cast<uint8_t*>(cp_buf) = ICMP_ECHO_REPLY;
    *reinterpret_cast<uint16_t*>(cp_buf + 2) = 0; // 清零，再算校验和
    uint16_t chksum = checksum(cp_buf, size);
    *reinterpret_cast<uint16_t*>(cp_buf + 2) = chksum;
    send_ipv4(src_ip, IP_PROTOCOL_ICMP, cp_buf, size);
    kfree(cp_buf);
}

void sockfs_icmp_add(int inode_id, char* buffer, size_t size);
void icmp_handler(uint16_t ip_header_size, char* buffer, uint16_t size) {
    if (checksum(buffer + ip_header_size, size - ip_header_size) != 0) {
        return;
    }
    // 根据类型做分类
    uint8_t type = *(reinterpret_cast<uint8_t*>(buffer + ip_header_size));
    // 这里如果构造恶意的icmp包，我的内核是能崩溃掉的...因为没有校验icmp长度是否合法。
    if (type == ICMP_ECHO_REQUEST) {
        handle_echo_request(ipv4addr(reinterpret_cast<ip_header*>(buffer)->src_ip), buffer + ip_header_size, size - ip_header_size);
    } else if (type == ICMP_ECHO_REPLY) {
        icmp_echo_head* head = (icmp_echo_head*)(buffer + ip_header_size);
        sockfs_icmp_add(head->id, buffer, size);
    }
}
