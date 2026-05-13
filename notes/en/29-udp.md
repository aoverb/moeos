## Homemade OS (29): UDP

```
This article is not complete... under construction.
```

Today we'll implement UDP and use it to support the DNS protocol.

#### UDP

UDP is essentially a simplified TCP — no state machine, but still has a hash table.

#### Packet Format

```
        0              15 16             31
       +-----------------+-----------------+
       | Source Port     |Destination Port |
       +-----------------+-----------------+
       |                 |                 |
       |     Length      |    Checksum     |
       +-----------------+-----------------+
       |                                   |
       |          data octets ...          |
       +-----------------------------------+
```

The UDP packet format is incredibly simple — almost laughably so...

As usual, let's start with a client:

```cpp
#include <net/net.hpp>
#include <net/socket.hpp>
#include <file.h>
#include <stdio.h>
#include <format.h>
#include <stdlib.h>
#include <poll.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: udp-client <ip addr> <port>\n");
        return 0;
    }

    int conn = open("/sock/udp", O_CREATE);
    if (conn == -1) {
        printf("udp unsupported!\n");
        return 0;
    }
    sockaddr bindaddr;
    bindaddr.addr = SOCKADDR_BROADCAST_ADDR;
    bindaddr.port = 8080;
    if (ioctl(conn, "SOCK_IOC_BIND", &bindaddr) < 0) {
        printf("failed to bind %s:%d", bindaddr.addr, bindaddr.port);
        return 0;
    }

    pollfd fds[2] = {
        { .fd = 0, .events = POLLIN, .revents = 0}, // stdin
        { .fd = conn, .events = POLLIN, .revents = 0 }
    };

    char buff[256];
    while(1) {
        int ret = poll(fds, 2, -1);  // -1 = infinite wait
        if (ret < 0) { break; }

        if (fds[0].revents & POLLIN) {
            int n = read(0, buff, sizeof(buff));
            if (n <= 0) break;
            if (strncmp("/bye\n", buff, n) == 0) {
                break;
            }
            write(conn, buff, n);
        }

        // socket has data → read and print
        if (fds[1].revents & POLLIN) {
            int n = read(conn, buff, sizeof(buff));
            if (n < 0) {
                printf("connection has been closed\n");
                break;
            }
            if (n == 0) continue;
            buff[n] = '\0';
            printf("%s\n", buff);
        }
    }

    close(conn);
    return 0;
}
```

#### Interface

```cpp
int udp_init(socket& sock, uint16_t local_port);
int udp_read(socket& sock, char* buffer, uint32_t size);
int udp_write(socket& sock, char* buffer, uint32_t size);
int udp_ioctl(TCBPtr& tcb, const char* cmd, void* arg);
int udp_close(socket& sock);
```

The interface is adapted from TCP's.

#### Implementation

UDP also has two hash tables like TCP, but the difference is we don't need a TCB (since there's no connection state). Our values directly hold the socket.

Binary tuple hash table + linked list vs. Two hash tables

We don't want to overcomplicate things, so we'll make UDP as similar to TCP as possible.

#### Echo Server

```cpp
        if (fds[0].revents & POLLIN) {
            int n = read(0, buff, sizeof(buff));
            if (n <= 0) break;
            if (strncmp("/bye\n", buff, n) == 0) {
                break;
            }
            write(conn, buff, n);
        }
```

The echo doesn't work here because we haven't captured the peer's connection info! We need the `recvfrom` interface to get connection information, and `sendto` to specify a temporary address and port for sending!

#### sendto, recvfrom

We also need an adaptation: when the address is `0.0.0.0`, replace it with the NIC's IP address.

![image-20260313193243224](../assets/自制操作系统（29）：UDP/image-20260313193243224.png)

---

Our networking journey comes to an end here!

In the next chapter, we'll implement signals.
