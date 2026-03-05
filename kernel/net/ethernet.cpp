#include <kernel/net/ethernet.hpp>
#include <driver/rtl8139.hpp>
#include <kernel/mm.hpp>
#include <string.h>
#include <stdio.h>

typedef struct {
    uint8_t target_mac[6];
    uint8_t source_mac[6];
    char type[2];
} __attribute__((packed)) ethernet_head;

void arp_handler(char* buffer, uint16_t size);

void ethernet_handler(char* buffer, uint16_t size) {
    printf("ethernet_handler: size=%d, type=%X %X\n",
        size, (uint8_t)buffer[12], (uint8_t)buffer[13]);
    if (size < sizeof(ethernet_head)) return;
    char* type = reinterpret_cast<ethernet_head*>(buffer)->type;
    // 注意网络传输用的是大端，但是这里我们逐个字节判断，没问题
    if (type[0] == 0x08 && type[1] == 0x06) { // ARP
        arp_handler(buffer + sizeof(ethernet_head), size - sizeof(ethernet_head));
    }
    return;
}

int send_ethernet_frame(const uint8_t target_mac[6], const uint8_t source_mac[6], const char type[2],
    void* buffer, uint16_t size) {
    if (size > 1536 - sizeof(ethernet_head)) return -1;
    // todo: 逐级包装又逐级销毁，时空开销不小！！！这里后面一定要改掉
    void* buf = kmalloc(sizeof(ethernet_head) + size);
    ethernet_head* head = (ethernet_head*)buf;
    memcpy(head->target_mac, target_mac, 6);
    memcpy(head->source_mac, source_mac, 6);
    memcpy(head->type, type, 2);
    memcpy(buf + sizeof(ethernet_head), buffer, size);
    int ret = nic_write((const char*)buf, sizeof(ethernet_head) + size);
    kfree(buf);
    return ret;
}