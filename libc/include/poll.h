#ifndef _POLL_H
#define _POLL_H 1
#include <stdint.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif
constexpr int8_t POLLIN = (1 << 0);
struct pollfd {
    int fd;
    uint8_t events;
    uint8_t revents;
};

int poll(pollfd* fds, uint32_t fd_num, uint32_t timeout) {
    return -1;
}

#ifdef __cplusplus
}
#endif

#endif
