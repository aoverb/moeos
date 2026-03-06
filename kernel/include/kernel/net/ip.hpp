#ifndef _KERNEL_NET_IP_HPP
#define _KERNEL_NET_IP_HPP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t header_len : 4;
    uint8_t version : 4; // 这里有坑，version和header_len构成一个字节，但是要转成大端的所以位置要调换
    uint8_t type_of_svc : 8;
    uint16_t total_len : 16;
    uint16_t id : 16;
    uint16_t flags_n_offset : 16;
    uint8_t ttl : 8;
    uint8_t protocol : 8;
    uint16_t checksum : 16;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed)) ip_header;

int send_ipv4(uint16_t id, uint8_t protocol, uint32_t src_ip, uint32_t dst_ip,
    void* payload, uint32_t payload_len, uint8_t ttl = 120);

#ifdef __cplusplus
}
#endif

#endif