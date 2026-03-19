#include <signal.h>
#include <syscall_def.hpp>

int kill(pid_t pid) {
    return syscall1((uint32_t)SYSCALL::KILL, (uint32_t)pid);
}
