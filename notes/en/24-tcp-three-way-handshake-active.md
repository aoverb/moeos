## Homemade Operating System (24): TCP (Part 1) — Three-Way Handshake (Active)

TCP is an incredibly complex protocol. I must admit I was worried whether my meager knowledge from college computer networking courses, now years old, would be enough to pull this off. But regardless, **let's start optimistically**.

### What is TCP?

The IP layer is unreliable — it doesn't guarantee delivery, doesn't guarantee ordering, and doesn't guarantee no duplicate delivery. To build a reliable transport layer on top of such an unreliable protocol, many clever people before us devised various approaches, boiling down to several key points:

```cpp
Basic Data Transfer
Reliability
Flow Control
Multiplexing
Connections
Precedence and Security
```

### Target State

Our target state this time is to implement the three-way handshake as the sender (active opener).

### TCP Header

```
  0                   1                   2                   3
  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |          Source Port          |       Destination Port        |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                        Sequence Number                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                    Acknowledgment Number                     |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |  Data |       |C|E|U|A|P|R|S|F|                               |
 | Offset| Rsrvd |W|C|R|C|S|S|Y|I|            Window             |
 |       |       |R|E|G|K|H|T|N|N|                               |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |           Checksum            |         Urgent Pointer        |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                    Options                    |    Padding    |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

The TCP header is shown above. Since we're only implementing a simplified TCP, we'll skip the Urgent Pointer and Options fields.

### TCP Pseudo Header

```
+------------------+
| Source IP (4B)   |
+------------------+
| Dest IP (4B)     |
+------------------+
| 0 | Proto | TCP Length |
+------------------+
```

TCP wants the checksum to include the source and destination IP addresses, but the TCP header itself doesn't carry this information. So when computing the checksum, we need to prepend this pseudo header to the TCP segment.

### User-Mode Program

This time we'll jump straight to a user-mode program. As always, our goal is to make the user-mode program work.

```cpp
#include <net/net.hpp>
#include <file.h>
#include <stdio.h>
#include <format.h>
#include <stdlib.h>
#include <poll.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: ping <ip addr> <port>\n");
        return 0;
    }
    const uint16_t src_port = 60001; // local port

    char ip_addr[16];
    uint16_t dst_port;
    snprintf(ip_addr, sizeof(ip_addr), "%s", argv[1]);
    dst_port = atoi(argv[2]);

    char tcp_open_path[64];
    snprintf(tcp_open_path, sizeof(tcp_open_path), "/sock/%s/tcp/%d/%d", ip_addr, src_port, dst_port);

    printf("tcp-sender: connecting to %s:%d using port %d...", ip_addr, dst_port, src_port);
    int conn = open(tcp_open_path, O_CREATE);
    if (conn == -1) {
        printf("tcp unsupported!\n");
        return 0;
    }

    auto tcp_cb = [&](size_t size) {
        int r_size = size > 1024 ? 1024 : size;
        char* buffer = (char*)malloc(r_size);
        if (read(conn, buffer, r_size)) {
            printf("%s\n", buffer);
        }
    };

    pollfd fds[2] = {
        { .fd = 0, .events = POLLIN }, // stdin
        { .fd = conn, .events = POLLIN },
    };

    char buff[256];
    while(1) {
        int ret = poll(fds, 2, -1);  // -1 = infinite wait
        if (ret < 0) { break; }

        if (fds[0].revents & POLLIN) {
            uint32_t n = read(0, buff, sizeof(buff));
            if (n <= 0) break;
            write(conn, buff, n);
        }

        // socket has data → read and print
        if (fds[1].revents & POLLIN) {
            uint32_t n = read(conn, buff, sizeof(buff));
            if (n <= 0) {
                printf("connection has been closed\n");
                break;
            }
            buff[n] = '\0';
            printf("%s\n", buff);
        }
    }

    close(conn);
    return 0;
}
```

Notice we're using a new header: `poll`. This is something I found in the POSIX standard (actually from Linux) — it lets you monitor any file descriptors you provide for events you care about. Since we've already turned stdin/stdout into file descriptors, why not use it!

So our flow becomes:

Create TCP connection → poll stdin and the connection → when woken up, check whether it's from stdin or the connection → output buffer contents or send.

#### poll stub

Let's create a stub for poll first, then implement creating a TCP socket and connecting.

```cpp
constexpr int8_t POLLIN = (1 << 0);
struct pollfd {
    int fd;
    uint8_t events;
    uint8_t revents;
};

int poll(pollfd* fds, uint32_t fd_num, uint32_t timeout) {
    return -1;
}
```

![image-20260309220124951](../assets/自制操作系统（24）：TCP（一）——三次握手（主动）/image-20260309220124951.png)

#### Socket-related Operations

Compared to regular files, sockets require many more operations, and these operations can't be cleanly expressed using basic `open`, `read`, `write` (even if you could, it would be ugly and shift type-checking to runtime). I think it's time to compromise and add a set of socket-specific operations to our VFS system... Let's just say we're adapting to POSIX standards! But I still won't accept a `socket()` syscall — I'll just treat all other files as not supporting socket operations like `bind`, `connect`, etc. But is this really the right approach?

At any rate, I'm facing a design decision. When I implemented ICMP support, I created `sockfs`, allowing me to use basic VFS operations for sending/receiving ICMP messages. But now that I need to support TCP, I find TCP requires many more operations. If I merge all these into VFS, the coupling becomes very high. But splitting out a separate socket type also feels wrong — a socket is already bound to a VFS file, so why shouldn't it be treated as a file? I'm not sure how to evolve this.

I think treating sockets as a special kind of file is the better approach. Following object-oriented logic, a socket should be seen as a subclass of file. It has all the operations files support, plus its own set of socket-specific logic.

Essentially, a socket is still a file. All the special socket operations mentioned above would be controlled through the `ioctl` interface. To comply with POSIX standards, I'll implement all the socket interfaces, but they'll ultimately call `ioctl` underneath. Will this work? — The implementation inside `ioctl` would become a switch-case, and forcing adaptation might cause problems...

Well then, let's split the socket interface into things that can be expressed via `ioctl` and things that can't. For the ones that can't, we'll encapsulate them using indirect pointers in `fs_operation`!

```cpp
struct fs_operation {
    int (*mount)(mounting_point* mp);
    int (*unmount)(mounting_point* mp);
    int (*open)(mounting_point* mp, const char* path, uint8_t mode);
    int (*close)(mounting_point* mp, uint32_t inode_id, uint32_t mode);
    int (*read)(mounting_point* mp, uint32_t inode_id, uint32_t offset, char* buffer, uint32_t size);
    int (*write)(mounting_point* mp, uint32_t inode_id, const char* buffer, uint32_t size);
    int (*opendir)(mounting_point* mp, const char* path);
    int (*readdir)(mounting_point* mp, uint32_t inode_id, uint32_t offset, dirent* out);
    int (*closedir)(mounting_point* mp, uint32_t inode_id);
    int (*stat)(mounting_point* mp, const char* path, file_stat* out);

    sock_operation* sock_opr;
};

struct sock_operation {
    int (*connect)(mounting_point* mp, uint32_t inode_id, const char* addr, uint16_t port);
};
```

There, done!

#### open

First, we need to adapt `open` for ICMP — we can't embed the address in the path anymore. We'll set the address via `connect`:

```cpp
static int connect(mounting_point* mp, uint32_t inode_id, const char* addr, uint16_t port) {
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    socket& sock = data->sock[inode_id];
    if (sock.ptcl == protocol::ICMP) {
        return icmp_connect(sock, addr, port);
    } else if (sock.ptcl == protocol::TCP) {
        return tcp_connect(sock, addr, port);
    }
}

int icmp_connect(socket& sock, const char* addr, uint16_t) {
    strcpy(sock.addr, addr);
    return 0;
}
```

#### connect

![image-20260310010820879](../assets/自制操作系统（24）：TCP（一）——三次握手（主动）/image-20260310010820879.png)

![image-20260310010841466](../assets/自制操作系统（24）：TCP（一）——三次握手（主动）/image-20260310010841466.png)

No response to the request.

![image-20260310011326987](../assets/自制操作系统（24）：TCP（一）——三次握手（主动）/image-20260310011326987.png)

Later I discovered the checksum was wrong. Also, 8.8.8.8 doesn't respond to SYN. I switched to my blog's server, and it triggered!

![image-20260310011817017](../assets/自制操作系统（24）：TCP（一）——三次握手（主动）/image-20260310011817017.png)

And I received subsequent packets too!

#### tcp_handler

But what do we do with subsequent packets once received? I think this should be a producer/consumer model — packets can be placed in a linked list within our socket's `data`. Once we have data, what do we do with it? For example, during the TCP three-way handshake, when we receive a packet from the outside, we need to process it according to our own state. So we need to record our state in `data` and transition states based on what we receive. Let's define a structure called the Transmission Control Block (TCB):

```cpp
struct TCB { // Transmission Control Block
    tcb_state state;
    char* window;
    size_t window_size;
};
```

And provide an initialization function:

```cpp
constexpr uint32_t DEFAULT_WINDOW_SIZE = (1 << 16) - 1;
constexpr uint32_t DEFAULT_WINDOW_SCALE = 1;

int tcp_init(socket& sock) {
    sock.data = kmalloc(sizeof(TCB));
    TCB* tcb = (TCB*)sock.data;
    tcb->state = tcb_state::CLOSED;
    tcb->window_size = DEFAULT_WINDOW_SIZE * DEFAULT_WINDOW_SCALE;
    tcb->window = (char*)kmalloc(tcb->window_size);
}
```

We can anticipate that this function will also be needed for passive connections in the future, so we should make it more complete and move the initialization logic currently in sockfs's `open` operation into it:

```cpp
int tcp_init(socket& sock, uint16_t local_port) {
    sock.ptcl = protocol::TCP;
    strcpy("127.0.0.1", sock.dst_addr); // default address, set later via connect
    sock.dst_port = 0; // same as above

    uint8_t src_addr[4];
    getLocalNetconf()->ip.to_bytes(src_addr);
    sprintf(sock.src_addr, "%d.%d.%d.%d", src_addr[0], src_addr[1], src_addr[2], src_addr[3]);
    sock.src_port = local_port;

    sock.data = kmalloc(sizeof(TCB));
    TCB* tcb = (TCB*)sock.data;
    tcb->state = tcb_state::CLOSED;
    tcb->window_size = DEFAULT_WINDOW_SIZE * DEFAULT_WINDOW_SCALE;
    tcb->window = (char*)kmalloc(tcb->window_size);
    return 1;
}
```

Now sockfs's `open` is clean:

```cpp
uint32_t init_new_socket(socketfs_data* data) {
    SpinlockGuard guard(data->socket_lock);
    uint32_t new_sock_num = 0;
    for (int i = 0; i < MAX_SOCK_NUM; ++i) {
        if (data->sock[i].valid == 0) {
            new_sock_num = i;
            data->sock[i].valid = 1;
            break;
        }
    }
    return new_sock_num;
}

static int open(mounting_point* mp, const char* protocol,  uint8_t mode) {
    ++protocol; // skip leading slash, todo: validate input
    if (!strlen(protocol)) {
        return -1;
    }
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;

    if (mode == O_CREATE) { // create a socket
        uint32_t new_sock_num = init_new_socket(data);
        socket& new_sock = data->sock[new_sock_num];
        if (new_sock_num == 0) { // max sockets reached
            return -1;
        }
        
        if (strcmp("icmp", protocol) == 0) {
            icmp_init(new_sock);
        } else if (strcmp("tcp", protocol) == 0) {
            tcp_init(new_sock, 60001);  // TODO: should have a global port pool
        } else {
            return -1; // unsupported protocol
        }
        return new_sock_num;
    } else { // open existing socket
        return -1; // todo: not yet supported
    }
}
```

Now we can implement putting packets into the receive window in `tcp_handler`. But we're faced with a first challenge: how do we know which socket a packet corresponds to? We need to maintain a mapping from (source address, source port, local address, local port) → Socket. We have `unordered_map`, so our implementation becomes:

```cpp
struct tcp_quadruple {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    bool operator==(const tcp_quadruple& o) const {
        return src_ip == o.src_ip && dst_ip == o.dst_ip &&
               src_port == o.src_port && dst_port == o.dst_port;
    }
} __attribute__((packed));

std::unordered_map<tcp_quadruple, socket*> map_to_sock;
```

And we need a hash function for the quadruple:

```cpp
struct tcp_hasher {
    size_t operator()(const tcp_quadruple& q) const {
        size_t seed = 0;
        auto combine = [&](uint32_t v) {
            // classic bit perturbation algorithm to prevent hash collisions
            seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };
        combine(q.src_ip);
        combine(q.dst_ip);
        combine((uint32_t)q.src_port << 16 | q.dst_port);
        return seed;
    }
};

std::unordered_map<tcp_quadruple, socket*, tcp_hasher> map_to_sock;
```

The hash function uses a bit perturbation algorithm.

Now in the handler, we can find the corresponding socket by looking up the hash table:

```cpp
void tcp_handler(uint16_t ip_header_size, char* buffer, uint16_t size) {
    printf("got a tcp packet! size: %d\n", size);
    tcp_header* header = reinterpret_cast<tcp_header*>(buffer + ip_header_size);
    uint32_t src_ip = reinterpret_cast<ip_header*>(buffer)->src_ip;
    uint32_t dst_ip = reinterpret_cast<ip_header*>(buffer)->dst_ip;
    uint16_t src_port = header->src_port;
    uint16_t dst_port = header->dst_port;

    auto itr = map_to_sock[tcp_quadruple {.src_ip = src_ip, .dst_ip = dst_ip,
                               .src_port = src_port, .dst_port = dst_port}]; // ???
```

...Wait, are *we* the source, or is the *other side* the source?? I'm confused.

This actually reveals a problem: when we wrote `tcp_quadruple`, we only used "source" and "destination" as **relative** descriptions. But we should record connections from the host's perspective. For packets arriving from the network, the data in the packet is from the network's perspective, so we need to mirror it — convert the network perspective to our own perspective.

So we need to do two things:

1. Use semantically precise field names in `tcp_quadruple`;
2. In the handler, flip the source and destination of incoming packets when building the `tcp_quadruple`.

```cpp
    printf("got a tcp packet! size: %d\n", size);
    tcp_header* header = reinterpret_cast<tcp_header*>(buffer + ip_header_size);
    uint32_t src_ip = reinterpret_cast<ip_header*>(buffer)->src_ip;
    uint32_t dst_ip = reinterpret_cast<ip_header*>(buffer)->dst_ip;
    uint16_t src_port = header->src_port;
    uint16_t dst_port = header->dst_port;

    auto itr = map_to_sock[tcp_quadruple {.local_ip = dst_ip, .remote_ip = src_ip,
                               .local_port = dst_port, .remote_port = src_port}];
```

Actually I got it wrong — `map_to_sock` should use `find`, not array access. Let's continue:

```cpp
    auto itr = map_to_sock.find(tcp_quadruple {.local_ip = dst_ip, .remote_ip = src_ip,
                               .local_port = dst_port, .remote_port = src_port});
    if (itr == map_to_sock.end()) { // not found in existing connections
        printf("discard.");
    } else {
        TCB* tcb = (TCB*)itr->second->data;
        // How should buffer data be organized inside the window??
    }
```

Now I'm stuck again. Should I just push the buffer data into the window? Oh, and we have a wait queue — waking up everyone in it might be a good idea. But what about the packets in the buffer? First-come-first-served? Wouldn't that cause data corruption?

I asked Claude, and oh — TCP is a byte-stream protocol. That means the window contains a continuous stream of data, and to a large extent, there's only one reader. If there really were multiple readers, even in regular operating systems, you'd get the data fragmentation issue I mentioned. Looks like I was still thinking in message-oriented protocol terms...

Since we haven't completed the three-way handshake yet, my original idea was to put TCP segments into the window, wake up the `connect` that's halfway through connection establishment, have it process further, then `connect` sends an ACK, transitioning state to ESTABLISHED. But this approach is clearly cumbersome, and SYNACK packets have no payload — nothing useful goes into the window... So, let's have `connect` go to sleep after sending SYN and wait for the state to become ESTABLISHED:

```cpp
...
    if (send_ipv4((ipv4addr(dst_addr)), IP_PROTOCOL_TCP, t_header, sizeof(tcp_header)) != 0) {
        return -1;
    }
    int ret = -1;
    uint32_t flags = spinlock_acquire(&(sock.lock));
    if (tcb->state == tcb_state::ESTABLISHED) {
        ret = 0;
    } else {
        {
            SpinlockGuard guard(process_list_lock);
            process_list[cur_process_id]->state = process_state::WAITING;
            insert_into_process_queue(sock.wait_queue, process_list[cur_process_id]);
        }
        spinlock_release(&(sock.lock), flags);
        timeout(&(sock.wait_queue), 3000);
        uint32_t flags = spinlock_acquire(&(sock.lock));
        if (tcb->state == tcb_state::ESTABLISHED) {
            ret = 0;
        }
    }
    spinlock_release(&(sock.lock), flags);
    kfree(header);
    return ret;
```

We also need to update the prompt in `tcp-sender`:

```cpp
    if (connect(conn, ip_addr, dst_port)) {
        printf("connection established failed!\n");
    }
```

![image-20260310121012494](../assets/自制操作系统（24）：TCP（一）——三次握手（主动）/image-20260310121012494.png)

Now we can see that the handshake is failing. We need to implement a state machine in the handler that transitions states based on our state and the flags in the incoming packet.

```cpp
    SpinlockGuard guard(itr->second->lock);
    TCB* tcb = (TCB*)itr->second->data;

    switch (tcb->state)
    {
    case tcb_state::CLOSED:
        return;
    case tcb_state::SYN_SENT:
        if (header->flags == ((uint8_t)tcp_flags::SYN | (uint8_t)tcp_flags::ACK)) {
            // construct ACK packet, send back
            printf("SYNACK!");
            // tcb->state = tcb_state::ESTABLISHED;
        }
        return;
    default:
    }
    // todo: write data to buffer
```

![image-20260310122504964](../assets/自制操作系统（24）：TCP（一）——三次握手（主动）/image-20260310122504964.png)

Now we can detect SYNACK!

Constructing the ACK packet involves the same pile of code we wrote in `connect` — time to extract it into a function:

(We need to properly manage seq and ack too)

```cpp
    tcb->seq = 0; // todo: should use timestamp + random number
    tcb->ack = 0;
```

Add SEQ and ACK tracking to TCB — what sequence number we'll send next, and what sequence number we expect to receive next.

```cpp
int send_tcp_pack(socket& sock, tcp_flags flags, const char* payload, size_t size) {
    void* packet = kmalloc(sizeof(pseudo_tcp_header) + sizeof(tcp_header) + size);
    uint32_t packet_size = sizeof(pseudo_tcp_header) + sizeof(tcp_header) + size;
    memset(packet, 0, packet_size);
    pseudo_tcp_header* p_header = (pseudo_tcp_header*)packet;
    tcp_header* t_header = (tcp_header*)((char*)packet + sizeof(pseudo_tcp_header));
    TCB* tcb = (TCB*)sock.data;
    int tmp[4];

    sscanf_s(sock.dst_addr, "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
    uint8_t dst_addr[4] = { (uint8_t)tmp[0], (uint8_t)tmp[1], (uint8_t)tmp[2], (uint8_t)tmp[3] };

    sscanf_s(sock.src_addr, "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
    uint8_t src_addr[4] = { (uint8_t)tmp[0], (uint8_t)tmp[1], (uint8_t)tmp[2], (uint8_t)tmp[3] };
    p_header->src_addr = ipv4addr(src_addr).addr;
    p_header->dst_addr = ipv4addr(dst_addr).addr;
    p_header->protocol = IP_PROTOCOL_TCP;
    p_header->zero = 0;
    p_header->tcp_length = htons(sizeof(tcp_header));

    t_header->src_port = htons(sock.src_port);
    t_header->dst_port = htons(sock.dst_port);
    t_header->seq_num = htonl(tcb->seq); // my sending sequence number
    t_header->ack_num = htonl(tcb->ack); // the sequence number the peer should send
    t_header->reserved = 0;
    t_header->data_offset = sizeof(tcp_header) / 4;
    t_header->flags = (uint8_t)flags;
    t_header->window = htons(tcb->window_size);
    t_header->checksum = 0;
    t_header->urgent_ptr = 0;
    memcpy(((char*)packet + sizeof(pseudo_tcp_header) + sizeof(tcp_header)), payload, size);
    t_header->checksum = checksum(packet, packet_size);
    map_to_sock[tcp_quadruple {.local_ip = p_header->src_addr, .remote_ip = p_header->dst_addr,
                               .local_port = t_header->src_port, .remote_port = t_header->dst_port}] = &sock;
    tcb->seq += size; // note the increment here
    int ret = send_ipv4((ipv4addr(dst_addr)), IP_PROTOCOL_TCP, t_header, sizeof(tcp_header) + size);
    printf("ret: %s\n", sock.dst_addr);
    kfree(packet);
    return ret;
}
```

Note that for certain special flags, even if the payload is empty, we need to increment by one virtual byte.

```c++
int tcp_connect(socket& sock, const char* addr, uint16_t port) {
    TCB* tcb = (TCB*)sock.data;
    
    strncpy(sock.dst_addr, addr, 16);
    sock.dst_port = port;
    // SEND SYN
    if (send_tcp_pack(sock, tcp_flags::SYN, nullptr, 0) < 0) return -1; 
    tcb->seq += 1; // virtual byte
    tcb->state = tcb_state::SYN_SENT;
    
    ...
    
    case tcb_state::SYN_SENT:
    if ((header->flags & ((uint8_t)tcp_flags::SYN | (uint8_t)tcp_flags::ACK)) != 
        ((uint8_t)tcp_flags::SYN | (uint8_t)tcp_flags::ACK)) {
            break;
    }
    tcb->ack = ntohl(header->seq_num) + 1; // next expected: SYN's seq + 1 virtual byte
    send_tcp_pack(*sock, tcp_flags::ACK, nullptr, 0);
    tcb->state = tcb_state::ESTABLISHED;
    {
        SpinlockGuard guard(process_list_lock);
        PCB* cur;
        while(cur = sock->wait_queue) {
            remove_from_process_queue(sock->wait_queue, cur->pid);
            cur->state = process_state::READY;
            insert_into_scheduling_queue(cur->pid);
        }
    }
    printf("connection established!");
    break;
```

Actually, we shouldn't manually add the virtual byte for SYN in the caller — it should be handled inside `send_tcp_pack`. We'll fix that later.

![image-20260310152539639](../assets/自制操作系统（24）：TCP（一）——三次握手（主动）/image-20260310152539639.png)

![image-20260310152553664](../assets/自制操作系统（24）：TCP（一）——三次握手（主动）/image-20260310152553664.png)

We successfully implemented the three-way handshake!

---

Next section, we'll implement the passive version of the three-way handshake.
