#ifndef _NET_NET_HPP
#define _NET_NET_HPP 1
#include <stdint.h>
#include <stddef.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif

constexpr uint32_t SOCKADDR_BROADCAST_ADDR = 0;

struct sockaddr {
    uint32_t addr;
    uint16_t port;
    bool operator==(const sockaddr& o) const {
        return addr == o.addr && port == o.port;
    }
};

enum class tcb_state {
    CLOSED, SYN_SENT, ESTABLISHED, LISTEN, SYN_RCVD
};

enum class tcp_flags : uint8_t {
    FIN = 1 << 0,
    SYN = 1 << 1,
    RST = 1 << 2,
    PSH = 1 << 3,
    ACK = 1 << 4,
    URG = 1 << 5,
    ECE = 1 << 6,
    CWR = 1 << 7
};

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  reserved : 4;
    uint8_t  data_offset : 4; // 注意这里跟上面的四字节位置掉转过来了
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed));

struct pseudo_tcp_header {
    uint32_t src_addr;
    uint32_t dst_addr;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcp_length;
} __attribute__((packed));

constexpr uint8_t IP_PROTOCOL_ICMP = 0x01;
constexpr uint8_t IP_PROTOCOL_TCP = 0x06;

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

constexpr uint8_t ICMP_ECHO_REPLY = 0x0;
constexpr uint8_t ICMP_ECHO_REQUEST = 0x8;

struct icmp_echo_head {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
};

inline uint16_t htons(uint16_t v) {
    return (v >> 8) | (v << 8);
}

inline uint16_t ntohs(uint16_t v) {
    return htons(v);
}

inline uint32_t htonl(uint32_t v) {
    return ((uint32_t)htons(v & 0xFFFF) << 16) |
           (uint32_t)htons(v >> 16);
}

inline uint32_t ntohl(uint32_t v) {
    return htonl(v);
}

uint16_t checksum(void* data, uint32_t size);


#ifdef __cplusplus
}
#endif

#endif
