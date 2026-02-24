#ifndef _KERNEL_SPINLOCK_H
#define _KERNEL_SPINLOCK_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct spinlock {
    volatile uint32_t locked = 0;
} spinlock;

void spinlock_acquire(spinlock* lock) {
    while(1) {
        uint32_t old = 1;
        asm volatile ("xchgl %0, %1"
        : "=r"(old), "+m"(lock->locked)
        : "0"(old)
        : "memory"
        );
        if (old == 0) break;
        asm volatile("pause");
    }
}

void spinlock_release(spinlock* lock) {
    lock->locked = 0;
}


#ifdef __cplusplus
}
#endif

#endif