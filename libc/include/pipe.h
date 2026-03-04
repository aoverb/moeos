#ifndef _PIPE_H
#define _PIPE_H 1

#include <stddef.h>
#include <stdint.h>
#include <syscall_def.hpp>

#define O_RDONLY  0x01
#define O_WRONLY  0x02
#define O_RDWR    0x03
#define O_CREATE  0x04

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fd_remap {
    int child_fd;
    int parent_fd;
} fd_remap;

#if defined(__is_libk)
int kpipe(int fds[2]);
#endif

static int pipe(int fds[2]) {
#if defined(__is_libk)
    kpipe(fds);
    // 内核实现...
#else
    return syscall1((uint32_t)SYSCALL::PIPE, (uint32_t)fds);
#endif
}

#ifdef __cplusplus
}
#endif

#endif