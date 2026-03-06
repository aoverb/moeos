#include <kernel/net/ip.hpp>
#include <kernel/net/net.hpp>
#include <stdio.h>

void reassemble(ip_header* header) {
    return;
}

void icmp_handler(char* buffer, uint16_t size);

void ip_handler(char* buffer, uint16_t size) {
    printf("it works!\n");
    ip_header* header = reinterpret_cast<ip_header*>(buffer);
    uint16_t header_size = header->header_len * 4; // header_len四个字节为单位
    if (checksum(header, header_size) != 0) {
        return;
    }
    uint16_t ip_total_len = ntohs(header->total_len);
    if (!is_same_ip(reinterpret_cast<uint8_t*>(&(header->dst_ip)), my_ip)) {
        return;
    }
    if (header->version != 4) { // 仅支持IPV4
        return;
    }
    if (header->header_len < 5) { // 头部太小
        return;
    }

    uint8_t flag = (header->flags_n_offset >> 13);
    if ((flag & 0b010) != 0) {// Don't fragment 位不为0，也就是要分片
        reassemble(header);
        return;
    }
    printf("header size: %d, total size: %d\n", header_size, ip_total_len);
    if (header->protocol == 0x01) { // ICMP
        icmp_handler(buffer + header_size, ip_total_len - header_size);
    }
    return;
}