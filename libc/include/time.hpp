#ifndef _TIME_HPP
#define _TIME_HPP 1

#include <stddef.h>
#include <stdint.h>
#include <syscall_def.hpp>

using clock_t = uint32_t;

#ifdef __cplusplus
extern "C" {
#endif

static int sleep(uint32_t second) {
    return syscall1((uint32_t)SYSCALL::SLEEP, (uint32_t)second);
}

static clock_t clock() {
    return syscall0((clock_t)SYSCALL::CLOCK);
}

#ifdef __cplusplus
}
#endif

#endif