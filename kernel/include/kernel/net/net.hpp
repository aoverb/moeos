#ifndef _KERNEL_NET_NET_HPP
#define _KERNEL_NET_NET_HPP
#include <stdint.h>
#include <string.h>
#include <net/net.hpp>

#ifdef __cplusplus
extern "C" {
#endif

extern "C++" {
typedef struct macaddr {
    static constexpr uint64_t MASK = 0x0000FFFFFFFFFFFF; // 低6字节有效

    uint64_t addr; // 网络字节序存储，仅低6字节有效

    macaddr(uint64_t input_addr = 0) : addr(input_addr & MASK) {}

    macaddr(const char* s) {
        addr = 0;
        memcpy(&addr, s, 6);
    }

    macaddr(const uint8_t* s) {
        addr = 0;
        memcpy(&addr, s, 6);
    }

    macaddr(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f) {
        addr = 0;
        uint8_t octets[6] = {a, b, c, d, e, f};
        memcpy(&addr, octets, 6);
    }

    bool operator==(const macaddr& rhs) const {
        return addr == rhs.addr;
    }

    bool operator!=(const macaddr& rhs) const {
        return addr != rhs.addr;
    }

    bool operator==(const uint8_t* rhs) const {
        return memcmp(&addr, rhs, 6) == 0;
    }

    bool operator!=(const uint8_t* rhs) const {
        return memcmp(&addr, rhs, 6) != 0;
    }

    friend bool operator==(const uint8_t* lhs, const macaddr& rhs) {
        return rhs == lhs;
    }

    friend bool operator!=(const uint8_t* lhs, const macaddr& rhs) {
        return rhs != lhs;
    }

    explicit operator uint64_t() const {
        return addr;
    }

    macaddr& operator=(const char* s) {
        addr = 0;
        memcpy(&addr, s, 6);
        return *this;
    }

    macaddr& operator=(const uint8_t* s) {
        addr = 0;
        memcpy(&addr, s, 6);
        return *this;
    }

    macaddr& operator=(uint64_t u) {
        addr = u & MASK;
        return *this;
    }

    void to_bytes(uint8_t out[6]) const {
        memcpy(out, &addr, 6);
    }
} macaddr;

struct ipv4addr {
    uint32_t addr = 0; // 网络字节序

    ipv4addr() = default;
    ipv4addr(const ipv4addr&) = default;

    explicit ipv4addr(uint32_t raw) : addr(raw) {}

    explicit ipv4addr(const char* s) {
        const uint8_t* u = reinterpret_cast<const uint8_t*>(s);
        memcpy(&addr, u, 4);
    }

    explicit ipv4addr(const uint8_t* u) {
        memcpy(&addr, u, 4);
    }

    explicit ipv4addr(const char* a, const char* b,
                      const char* c, const char* d) {
        uint8_t octets[4] = {
            (uint8_t)atoi(a), (uint8_t)atoi(b),
            (uint8_t)atoi(c), (uint8_t)atoi(d)
        };
        memcpy(&addr, octets, 4);
    }

    ipv4addr& operator=(const ipv4addr&) = default;

    ipv4addr& operator=(const uint8_t* u) {
        memcpy(&addr, u, 4);
        return *this;
    }

    ipv4addr& operator=(const char* s) {
        const uint8_t* u = reinterpret_cast<const uint8_t*>(s);
        memcpy(&addr, u, 4);
        return *this;
    }

    bool operator==(const ipv4addr& rhs) const { return addr == rhs.addr; }
    bool operator!=(const ipv4addr& rhs) const { return addr != rhs.addr; }

    bool operator==(const uint8_t* rhs) const { return memcmp(&addr, rhs, 4) == 0; }
    bool operator!=(const uint8_t* rhs) const { return memcmp(&addr, rhs, 4) != 0; }

    explicit operator uint32_t() const { return addr; }

    void to_bytes(uint8_t out[4]) const { memcpy(out, &addr, 4); }
};
}

const macaddr broadcast_mac = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

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
