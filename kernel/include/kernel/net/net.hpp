#ifndef _KERNEL_NET_NET_HPP
#define _KERNEL_NET_NET_HPP
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

inline uint16_t htons(uint16_t v) {
    return (v >> 8) | (v << 8);
}

const uint8_t my_ip[] = {
    10, 0, 1, 1
};

#ifdef __cplusplus
}
#endif

#endif
