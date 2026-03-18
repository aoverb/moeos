#include <sys/time.h>
#include <syscall_def.hpp>

time_t time(time_t *tloc) {
    time_t t = syscall0((uint32_t)SYSCALL::CLOCK);
    if (tloc) *tloc = t;
    return t;
}