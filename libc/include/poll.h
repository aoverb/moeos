#ifndef _POLL_H
#define _POLL_H 1
#include <stdint.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif
constexpr int16_t POLLIN = (1 << 0);
constexpr int16_t POLLHUP = (1 << 1);
constexpr int16_t INVFD = (1 << 7);
constexpr uint16_t ERROR = (1 << 8);
struct pollfd {
    int fd;
    uint16_t events;
    uint16_t revents;
};

int poll(pollfd* fds, uint32_t fd_num, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif
