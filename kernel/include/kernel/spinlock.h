#ifndef _KERNEL_SPINLOCK_H
#define _KERNEL_SPINLOCK_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct spinlock {
    volatile uint32_t locked = 0;
} spinlock;

static uint32_t spinlock_acquire(spinlock* lock) {
    uint32_t flags;
    asm volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    while (1) {
        uint32_t old = 1;
        asm volatile ("xchgl %0, %1"
            : "=r"(old), "+m"(lock->locked)
            : "0"(old)
            : "memory"
        );
        if (old == 0) break;
        asm volatile("pause");
    }
    return flags;
}

static void spinlock_release(spinlock* lock, uint32_t flags) {
    lock->locked = 0;
    asm volatile ("pushl %0; popfl" : : "r"(flags) : "memory");
}

#ifdef __cplusplus
}
#endif

#endif