#include <sys/wait.h>
#include <syscall_def.hpp>

int waitpid(int pid) {
    return syscall1((uint32_t)SYSCALL::WAITPID, (int)pid);
}