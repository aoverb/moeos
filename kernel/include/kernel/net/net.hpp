#ifndef _KERNEL_NET_NET_HPP
#define _KERNEL_NET_NET_HPP
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t checksum(void* data, uint32_t size);

bool is_same_ip(const uint8_t* ip1, const uint8_t* ip2);

inline uint16_t htons(uint16_t v) {
    return (v >> 8) | (v << 8);
}

inline uint16_t ntohs(uint16_t v) {
    return htons(v);
}

const uint8_t my_ip[] = {
    10, 0, 1, 1
};

#ifdef __cplusplus
}
#endif

#endif
