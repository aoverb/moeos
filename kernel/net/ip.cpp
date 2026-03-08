#include <kernel/net/ip.hpp>
#include <kernel/net/ethernet.hpp>
#include <kernel/net/net.hpp>
#include <kernel/net/arp.hpp>
#include <kernel/mm.hpp>
#include <stdio.h>
#include <string.h>

void reassemble(ip_header* header) {
    return;
}

int fragment_send_ipv4(const ipv4addr& dst_ip,
    const void* payload, uint32_t payload_len, uint8_t ttl) { return -1; }

int send_ipv4(const ipv4addr& dst_ip, uint8_t protocol,
    const void* payload, uint32_t payload_len, uint8_t ttl) {
    if (payload_len >= 576 - sizeof(ip_header)) { // 超过576的阈值，需要分片
        return fragment_send_ipv4(dst_ip, payload, payload_len, ttl);
    }
    uint16_t total_len = sizeof(ip_header) + payload_len;
    uint8_t* data = (uint8_t*)kmalloc(total_len);

    ip_header* head = reinterpret_cast<ip_header*>(data);
    head->checksum = 0;
    head->dst_ip = (uint32_t)dst_ip;
    head->flags_n_offset = 0;
    head->header_len = sizeof(ip_header) / 4;
    head->id = 0;
    head->protocol = protocol;
    head->src_ip = (uint32_t)(getLocalNetconf()->ip);
    head->total_len = htons(total_len);
    head->ttl = ttl;
    head->type_of_svc = 0;
    head->version = 0x4;
    head->checksum = checksum(head, sizeof(ip_header));
    memcpy(data + sizeof(ip_header), payload, payload_len);

    // 查ARP表
    macaddr dst_mac;
    ipv4addr dst_ip_x = ipv4addr(head->dst_ip);

    uint8_t dst_ip_arr[4];
    dst_ip_x.to_bytes(dst_ip_arr);
    if (!arp_table_lookup(dst_ip_x, dst_mac)) {
        // todo: 也许需要包装一个解析ip->mac的函数
        printf("warning: mac addr of dst_ip_arr %d.%d.%d.%d not found!", dst_ip_arr[0],
                                                                         dst_ip_arr[1],
                                                                         dst_ip_arr[2],
                                                                         dst_ip_arr[3]);
        return -1;
    }
    int ret = send_ethernet_frame(dst_mac, getLocalNetconf()->mac, TYPE_IP, data, total_len);
    kfree(data);
    return ret;
}

void icmp_handler(uint16_t ip_header_size, char* buffer, uint16_t size);

void ip_handler(char* buffer, uint16_t size) {
    ip_header* header = reinterpret_cast<ip_header*>(buffer);
    uint16_t header_size = header->header_len * 4; // header_len四个字节为单位
    if (checksum(header, header_size) != 0) {
        return;
    }
    uint16_t ip_total_len = ntohs(header->total_len);
    if (ipv4addr(header->dst_ip) != getLocalNetconf()->ip) {
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
    if (header->protocol == IP_PROTOCOL_ICMP) {
        icmp_handler(header_size, buffer, ip_total_len);
    }
    return;
}