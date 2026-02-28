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
    MOUNT = 100,
    UNMOUNT = 101,
    OPEN = 102,
    READ = 103,
    WRITE = 104,
    CLOSE = 105,
    OPENDIR = 106,
    READDIR = 107,
    CLOSEDIR = 108,
    EXEC = 200
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

#ifdef __cplusplus
}
#endif

#endif
