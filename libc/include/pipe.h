#ifndef _PIPE_H
#define _PIPE_H 1

#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>

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

int pipe(int fds[2]);

#ifdef __cplusplus
}
#endif

#endif