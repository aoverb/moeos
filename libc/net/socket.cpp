#include <net/socket.hpp>
#include <syscall_def.hpp>


int connect(int fd, const char* addr, uint16_t port) {
    return syscall3((uint32_t)SYSCALL::CONNECT, (uint32_t)fd, (uint32_t)addr, (uint32_t)port);
}

int listen(int fd, size_t queue_length) {
    return syscall2((uint32_t)SYSCALL::LISTEN, (uint32_t)fd, (uint32_t)queue_length);
}

int accept(int fd, sockaddr* peeraddr, size_t* size) {
    return syscall3((uint32_t)SYSCALL::ACCEPT, (uint32_t)fd, (uint32_t)peeraddr, (uint32_t)size);
}