#ifndef _UNISTD_H
#define _UNISTD_H 1

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef int pid_t;
pid_t getpid();

#ifdef __cplusplus
}
#endif

#endif