#include <stdarg.h>
#include <syscall_def.hpp>

extern "C" int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void* arg = va_arg(ap, void*);
    va_end(ap);
    return syscall3((uint32_t)SYSCALL::IOCTL, (uint32_t)fd, (uint32_t)request, (uint32_t)arg);
}