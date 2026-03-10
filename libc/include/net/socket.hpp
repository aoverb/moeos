#ifndef _NET_SOCKET_HPP
#define _NET_SOCKET_HPP
#include <net/net.hpp>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif
int connect(int fd, const char* addr, uint16_t port);
int listen(int fd, size_t queue_length);
int accept(int fd, sockaddr* peeraddr, size_t* size);
#ifdef __cplusplus
}
#endif

#endif
