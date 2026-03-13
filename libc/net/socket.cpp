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

int sendto(int fd, const char* buffer, uint32_t size, sockaddr* peeraddr) {
    return syscall4((uint32_t)SYSCALL::SENDTO, (uint32_t)fd, (uint32_t)buffer, (uint32_t)size, (uint32_t)peeraddr);
}

int recvfrom(int fd, char* buffer, uint32_t size, sockaddr* peeraddr) {
    return syscall4((uint32_t)SYSCALL::RECVFROM, (uint32_t)fd, (uint32_t)buffer, (uint32_t)size, (uint32_t)peeraddr);
}