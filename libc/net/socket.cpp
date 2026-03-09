#include <net/socket.hpp>
#include <syscall_def.hpp>

int connect(int fd, const char* addr, uint16_t port) {
    return syscall3((uint32_t)SYSCALL::CONNECT, (uint32_t)fd, (uint32_t)addr, (uint32_t)port);
}