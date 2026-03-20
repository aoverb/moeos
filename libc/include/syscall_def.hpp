#ifndef _SYSCALL_DEF_H
#define _SYSCALL_DEF_H 1
#include <stdint.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif
enum class SYSCALL {
    EXIT = 0,
    TERMINAL_WRITE = 1,
    TERMINAL_SET_TEXT_COLOR = 2,
    TERMINAL_GET_LINE = 3,
    TERMINAL_CLEAR = 4,
    TCSETPGRP = 13,
    KILL = 24,
    CLOCK = 25,
    YIELD = 26,
    SBRK = 50,
    CONNECT = 75,
    LISTEN = 76,
    ACCEPT = 77,
    SENDTO = 78,
    RECVFROM = 79,
    IOCTL = 98,
    STAT = 99,
    MOUNT = 100,
    UNMOUNT = 101,
    OPEN = 102,
    READ = 103,
    WRITE = 104,
    CLOSE = 105,
    OPENDIR = 106,
    READDIR = 107,
    CLOSEDIR = 108,
    CHDIR = 109,
    GETCWD = 110,
    UNLINK = 111,
    MKDIR = 112,
    TRUNCATE = 113,
    LSEEK = 114,
    PIPE = 199,
    EXEC = 200,
    WAITPID = 201,
    SLEEP = 202,
    POLL = 203,
    SETPGID = 204,
    GETPID = 205
};

enum SYSCALL_RET {
    FAILED = -1,
    SUCCESS = 0,
    SYSCALL_NOT_FOUND = 0XFF
};


static inline int syscall0(uint32_t num) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num));
    return ret;
}

static inline int syscall1(uint32_t num, uint32_t arg1) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1));
    return ret;
}

static inline int syscall2(uint32_t num, uint32_t arg1, uint32_t arg2) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2));
    return ret;
}

static inline int syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3));
    return ret;
}

static inline int syscall4(uint32_t num, uint32_t arg1, uint32_t arg2,
                           uint32_t arg3, uint32_t arg4) {
    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4));
    return ret;
}

static inline int syscall5(uint32_t num, uint32_t arg1, uint32_t arg2,
                           uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    int ret;
    asm volatile("push %%ebp\n\t"
                 "mov %[a5], %%ebp\n\t"
                 "int $0x80\n\t"
                 "pop %%ebp"
                 : "=a"(ret)
                 : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3),
                   "S"(arg4), [a5]"m"(arg5)
                 : "memory");
    return ret;
}

static inline int syscall6(uint32_t num, uint32_t arg1, uint32_t arg2,
                           uint32_t arg3, uint32_t arg4, uint32_t arg5,
                           uint32_t arg6) {
    int ret;
    asm volatile("push %%ebp\n\t"
                 "mov %[a5], %%ebp\n\t"
                 "int $0x80\n\t"
                 "pop %%ebp"
                 : "=a"(ret)
                 : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3),
                   "S"(arg4), "D"(arg6), [a5]"m"(arg5)
                 : "memory");
    return ret;
}

#ifdef __cplusplus
}
#endif

#endif
