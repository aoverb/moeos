#include <unistd.h>
#include <syscall_def.hpp>

pid_t getpid() {
    return syscall0((uint32_t)SYSCALL::GETPID);
}