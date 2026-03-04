#ifndef _DRIVER_PIPEFS_H
#define _DRIVER_PIPEFS_H

#include <stdint.h>
#include <string>
#include <unordered_map>

#ifdef __cplusplus
extern "C" {
#endif

int kpipe(int fds[2]);
void init_pipefs();

#ifdef __cplusplus
}
#endif

#endif