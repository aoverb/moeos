#ifndef _KERNEL_SPINLOCK_H
#define _KERNEL_SPINLOCK_H

#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct spinlock {
    volatile uint32_t locked = 0;
} spinlock;

static inline uint32_t spinlock_acquire(spinlock* lock);
static inline void spinlock_release(spinlock* lock, uint32_t flags);

#ifdef __cplusplus

class SpinlockGuard {
public:
    explicit SpinlockGuard(spinlock& lock) : lock_(lock) {
        flags_ = spinlock_acquire(&lock_);
    }

    ~SpinlockGuard() {
        
        spinlock_release(&lock_, flags_);
        
    }

    uint32_t flags() const { return flags_; }
    explicit operator uint32_t() const { return flags_; }

    SpinlockGuard(const SpinlockGuard&) = delete;
    SpinlockGuard& operator=(const SpinlockGuard&) = delete;
    SpinlockGuard(SpinlockGuard&&) = delete;
    SpinlockGuard& operator=(SpinlockGuard&&) = delete;

private:
    spinlock& lock_;
    uint32_t  flags_;
};

#endif

static inline uint32_t spinlock_acquire(spinlock* lock) {
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
        asm volatile ("pause");
    }
    return flags;
}

static inline void spinlock_release(spinlock* lock, uint32_t flags) {
    asm volatile ("" ::: "memory");
    lock->locked = 0;
    asm volatile ("pushl %0; popfl" : : "r"(flags) : "memory");
}

#ifdef __cplusplus
}
#endif

#endif