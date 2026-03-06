#include <kernel/net/arp.hpp>
#include <kernel/net/net.hpp>
#include <driver/rtl8139.hpp>
#include <kernel/net/ethernet.hpp>
#include <stdio.h>
#include <string.h>

#include <unordered_map>

extern "C" int __cxa_atexit(void (*)(void*), void*, void*) {
    return 0; // 内核不需要析构全局对象
}

std::unordered_map<uint32_t, uint64_t> ip_to_mac;

bool arp_table_lookup(uint8_t* ip, uint8_t* mac) {
    uint32_t t_ip = 0;
    for (int i = 0; i < 4; ++i) {
        t_ip = (t_ip << 8) + ip[4 - i - 1];
    }
    if (ip_to_mac.find(t_ip) == ip_to_mac.end()) return false;
    uint64_t t_mac = ip_to_mac[t_ip];
    for (int i = 0; i < 6; ++i) {
        mac[6 - i - 1] = t_mac & 0xff;
        t_mac >>= 8;
    }
    return true;
}

void arp_table_insert(uint8_t* ip, uint8_t* mac) {
    uint32_t t_ip = 0;
    for (int i = 0; i < 4; ++i) {
        t_ip = (t_ip << 8) + ip[4 - i - 1];
    }
    uint64_t t_mac = 0;
    for (int i = 0; i < 6; ++i) {
        t_mac = (t_mac << 8) + mac[6 - i - 1];
    }
    ip_to_mac[t_ip] = t_mac;
    printf("new arp record:\n");
    printf("ip: %d:%d:%d:%d\n", ip[0], ip[1], ip[2], ip[3]);
    printf("mac: %X:%X:%X:%X:%X:%X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

typedef struct {
    uint16_t hw_type;       // 硬件类型，以太网 = 0x0001
    uint16_t proto_type;    // 协议类型，IPv4 = 0x0800
    uint8_t  hw_len;        // 硬件地址长度，6
    uint8_t  proto_len;     // 协议地址长度，4
    uint16_t opcode;        // 1 = request 2 = reply
    uint8_t  sender_mac[6];
    uint8_t  sender_ip[4];
    uint8_t  target_mac[6];
    uint8_t  target_ip[4];
} __attribute__((packed)) arp_packet;

const uint8_t broadcast_mac[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

void to_print_mac(uint8_t* ip) {
    send_arp(htons(APR_OPCODE_REQ), broadcast_mac, ip);
}

uint8_t* my_mac() {
    static uint8_t flag = 0;
    static uint8_t mac[6];
    if (flag) return mac;
    get_mac(mac);
    flag = 1;
    return mac;
}

bool is_same_mac(const uint8_t* mac1, const uint8_t* mac2) {
    for (int i = 0; i < 6; ++i) {
        if (mac1[i] != mac2[i]) return false;
    }
    return true;
}

// 场景1.我要问别人MAC
// 或者响应别人对本机IP->MAC的询问
int send_arp(uint16_t opcode, const uint8_t* target_mac, const uint8_t* target_ip) {
    arp_packet header;
    header.hw_type = htons(0x0001);
    header.proto_type = htons(0x0800);
    header.hw_len = 6;
    header.proto_len = 4;
    header.opcode = opcode;
    memcpy(header.sender_mac, my_mac(), 6);
    memcpy(header.sender_ip, my_ip, 4);
    memcpy(header.target_mac, target_mac, 6);
    memcpy(header.target_ip, target_ip, 4);
    // 以太帧目标：request 用广播，reply 用对方 MAC
    const uint8_t* eth_dst = (opcode == htons(APR_OPCODE_REQ))
        ? (uint8_t*)broadcast_mac
        : target_mac;
    return send_ethernet_frame(eth_dst, my_mac(), TYPE_ARP, &header, sizeof(header));
}

void arp_handler(char* buffer, uint16_t size) {
    arp_packet* header = reinterpret_cast<arp_packet*>(buffer);
    // 仅接受<ip, 以太网>的映射
    if (header->hw_type != htons(0x0001) || header->proto_type != htons(0x0800)) return;
    if (header->opcode == htons(APR_OPCODE_REQ)) { // 请求
        if (is_same_ip(my_ip, header->target_ip)) { // 场景3：别人问mac，如果目标ip就是我
                send_arp(htons(APR_OPCODE_REPLY), header->sender_mac, header->sender_ip); // 我来响应
            }
    } else if (header->opcode == htons(APR_OPCODE_REPLY) && is_same_mac(my_mac(), header->target_mac)) {
        // 场景2：别人给我回复它的MAC
        arp_table_insert(header->sender_ip, header->sender_mac);
    }
}