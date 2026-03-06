#ifndef _KERNEL_NET_NET_HPP
#define _KERNEL_NET_NET_HPP
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

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

extern "C++" {
typedef struct macaddr {
    uint64_t addr; // 使用网络字节序存储
    macaddr(const char* s) {
        *this = s;
    }
    macaddr(const uint8_t* s) {
        *this = s;
    }
    macaddr(const char* a, const char* b, const char* c, const char* d, const char* e, const char* f) {
        uint8_t octets[6] = {
            (uint8_t)atoi(a), (uint8_t)atoi(b),
            (uint8_t)atoi(c), (uint8_t)atoi(d),
            (uint8_t)atoi(e), (uint8_t)atoi(f)
        };
        memcpy(&addr, octets, 6);
    }
    bool operator == (macaddr& rhs) const {
        return addr == rhs.addr;
    }
    
    bool operator == (const uint8_t* rhs) const {
        return memcmp(&addr, rhs, 6) == 0;
    }
    friend bool operator==(const uint8_t* lhs, const macaddr& rhs) {
        return rhs == lhs;
    }

    bool operator != (const uint8_t* rhs) const {
        return memcmp(&addr, rhs, 6) != 0;
    }
    friend bool operator!=(const uint8_t* lhs, const macaddr& rhs) {
        return rhs != lhs;
    }
    
    operator uint64_t() const {
        return addr;
    }

    macaddr operator =(const char* s) {
        addr = 0;
        const unsigned char* u = reinterpret_cast<const unsigned char*>(s);
        uint8_t octets[6] = {u[0], u[1], u[2], u[3], u[4], u[5]};
        memcpy(&addr, octets, 6);
        return *this;
    }
    macaddr operator =(const uint8_t* u) {
        addr = 0;
        memcpy(&addr, u, 6);
        return *this;
    }
    void to_bytes(uint8_t out[6]) const {
        memcpy(out, &addr, 6);
    }
} macaddr;

typedef struct ipv4addr {
    uint32_t addr; // 使用网络字节序存储
    ipv4addr(const char* s) {
        *this = s;
    }
    ipv4addr(const char* a, const char* b, const char* c, const char* d) {
        uint8_t octets[4] = {
            (uint8_t)atoi(a), (uint8_t)atoi(b),
            (uint8_t)atoi(c), (uint8_t)atoi(d)
        };
        memcpy(&addr, octets, 4);
    }
    bool operator == (ipv4addr& rhs) const {
        return addr == rhs.addr;
    }
    
    bool operator == (const uint8_t* rhs) const {
        return addr == htonl((rhs[0] << 24) | (rhs[1] << 16) | (rhs[2] << 8) | rhs[3]);
    }
    friend bool operator==(const uint8_t* lhs, const ipv4addr& rhs) {
        return rhs == lhs; // 复用上面的实现
    }

    bool operator != (const uint8_t* rhs) const {
        return addr != htonl((rhs[0] << 24) | (rhs[1] << 16) | (rhs[2] << 8) | rhs[3]);
    }
    friend bool operator!=(const uint8_t* lhs, const ipv4addr& rhs) {
        return rhs != lhs; // 复用上面的实现
    }
    
    operator uint32_t() const {
        return addr;
    }

    ipv4addr operator =(const char* s) {
        const unsigned char* u = reinterpret_cast<const unsigned char*>(s);
        uint8_t octets[4] = {u[0], u[1], u[2], u[3]};
        memcpy(&addr, octets, 4);
        return *this;
    }
    void to_bytes(uint8_t out[4]) const {
        memcpy(out, &addr, 4);
    }
} ipv4addr;
}

uint16_t checksum(void* data, uint32_t size);

struct netconf {
    ipv4addr ip;
    ipv4addr mask;
    macaddr mac;
};

void init_netconf();

const netconf* getLocalNetconf();

#ifdef __cplusplus
}
#endif

#endif
