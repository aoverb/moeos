#include <kernel/net/tcp.hpp>
#include <kernel/mm.h>
#include <format.h>

int tcp_connect(socket& sock, const char* addr, uint16_t port) {
    // SEND SYN
    void* header = kmalloc(sizeof(pseudo_tcp_header) + sizeof(tcp_header));
    memset(header, 0, sizeof(pseudo_tcp_header) + sizeof(tcp_header));
    pseudo_tcp_header* p_header = (pseudo_tcp_header*)header;
    tcp_header* t_header = (tcp_header*)((char*)header + sizeof(pseudo_tcp_header));

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
    t_header->ack_num = 0;
    t_header->reserved = 0;
    t_header->data_offset = sizeof(tcp_header) / 4;
    t_header->flags = 0;
    t_header->flags = (uint8_t)tcp_flags::SYN;
    t_header->window = htons((1 << 16) - 1);
    t_header->checksum = 0;
    t_header->urgent_ptr = 0;

    t_header->checksum = checksum(header, sizeof(pseudo_tcp_header) + sizeof(tcp_header));

    send_ipv4((ipv4addr(dst_addr)), IP_PROTOCOL_TCP, t_header, sizeof(tcp_header));
    kfree(header);
    return 0;
}

void tcp_handler(uint16_t ip_header_size, char* buffer, uint16_t size) {
    printf("got a tcp packet! size: %d\n", size);
}