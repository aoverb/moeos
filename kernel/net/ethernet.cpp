#include <kernel/net/ethernet.hpp>
#include <driver/rtl8139.hpp>
#include <kernel/mm.hpp>
#include <string.h>

typedef struct ethernet_head {
    char target_mac[6];
    char source_mac[6];
    char type[2];
} __attribute__((packed));

int send_ethernet_frame(char target_mac[6], char source_mac[6], char type[2],
    char* buffer, uint16_t size) {
    if (size > 1536 - sizeof(ethernet_head)) return -1;
    void* buf = kmalloc(sizeof(ethernet_head) + size);
    ethernet_head* head = (ethernet_head*)buf;
    memcpy(head->target_mac, target_mac, 6);
    memcpy(head->source_mac, source_mac, 6);
    memcpy(head->type, type, 2);
    memcpy(buf + sizeof(ethernet_head), buffer, size);
    return nic_write((const char*)buf, sizeof(ethernet_head) + size);
}