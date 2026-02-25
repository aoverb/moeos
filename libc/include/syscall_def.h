#ifndef _SYSCALL_DEF_H
#define _SYSCALL_DEF_H 1
#include <stdint.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif
enum class SYSCALL {
    EXIT = 0,
    TERMINAL_WRITE = 1
};

enum class SYSCALL_RET {
    SUCCESS = 0,
    SYSCALL_NOT_FOUND = 0XFF
};


static inline uint32_t syscall0(uint32_t num) {
    uint32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num));
    return ret;
}

static inline uint32_t syscall1(uint32_t num, uint32_t arg1) {
    uint32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1));
    return ret;
}

static inline uint32_t syscall2(uint32_t num, uint32_t arg1, uint32_t arg2) {
    uint32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2));
    return ret;
}

#ifdef __cplusplus
}
#endif

#endif
