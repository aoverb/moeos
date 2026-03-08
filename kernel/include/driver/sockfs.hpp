#ifndef _DRIVER_SOCKFS_H
#define _DRIVER_SOCKFS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

constexpr uint16_t MAX_SOCK_NUM = 1024;
void init_sockfs();

#ifdef __cplusplus
}
#endif

#endif