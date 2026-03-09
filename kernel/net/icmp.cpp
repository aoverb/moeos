#include <kernel/net/icmp.hpp>
#include <kernel/net/ip.hpp>
#include <kernel/net/net.hpp>
#include <kernel/net/socket.hpp>
#include <kernel/mm.hpp>
#include <stdio.h>

int icmp_connect(socket& sock, const char* addr, uint16_t) {
    strcpy(sock.dst_addr, addr);
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
