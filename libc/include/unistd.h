#ifndef _UNISTD_H
#define _UNISTD_H 1

#include <stddef.h>
#include <stdint.h>
#include <syscall_def.hpp>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1

#ifdef __cplusplus
extern "C" {
#endif
typedef int pid_t;
pid_t getpid();

static int isatty(int fd) {
    return (fd >= 0 && fd <= 1) ? 1 : 0; // 简单实现
}

static int ftruncate(int fd, uint32_t length) {
    return syscall2((uint32_t)SYSCALL::TRUNCATE, (uint32_t)fd, (uint32_t)length);
}
#ifdef __cplusplus
}
#endif

#endif