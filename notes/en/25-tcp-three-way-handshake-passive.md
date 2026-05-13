## Homemade Operating System (25): TCP (Part 2) — Three-Way Handshake (Passive)

In the previous section, we embarked on our TCP journey, implementing the active creation of a TCP connection via three-way handshake. In this section, we'll implement the passive three-way handshake.

### Target State

As usual, let's start with the target state user program `tcp-server`:

```cpp
#include <net/net.hpp>
#include <net/socket.hpp>
#include <file.h>
#include <stdio.h>
#include <format.h>
#include <stdlib.h>
#include <poll.h>

int main(int argc, char** argv) {
    int conn = open("/sock/tcp", O_CREATE);
    if (conn == -1) {
        printf("tcp unsupported!\n");
        return 0;
    }
    sockaddr bindaddr;
    bindaddr.port = 8080;
    strcpy(bindaddr.addr, SOCKADDR_BROADCAST_ADDR); 
    if (ioctl(conn, "SOCK_IOC_BIND", &bindaddr) < 0) {
        printf("failed to bind %s:%d", bindaddr.addr, bindaddr.port);
        return 0;
    }

    if (listen(conn, 5)) {
        printf("failed to listen!\n");
    }

    int client_fd;
    while (client_fd = accept(conn, nullptr, nullptr)) {
        if (client_fd == -1) break;
        pollfd fds[2] = {
            { .fd = 0, .events = POLLIN, .revents = 0}, // stdin
            { .fd = client_fd, .events = POLLIN, .revents = 0 }
        };

        char buff[256];
        while(1) {
            int ret = poll(fds, 2, -1);  // -1 = infinite wait
            if (ret < 0) { break; }

            if (fds[0].revents & POLLIN) {
                uint32_t n = read(0, buff, sizeof(buff));
                if (n <= 0) break;
                write(client_fd, buff, n);
            }

            // socket has data → read and print
            if (fds[1].revents & POLLIN) {
                uint32_t n = read(client_fd, buff, sizeof(buff));
                if (n <= 0) {
                    printf("connection has been closed\n");
                    break;
                }
                buff[n] = '\0';
                printf("%s\n", buff);
            }
        }
    }

    close(conn);
    return 0;
}
```

Serially accept connections from clients, with a maximum of 5. I won't go into the stubbing details.

![image-20260310180540171](../assets/自制操作系统（25）：TCP（二）——三次握手（被动）/image-20260310180540171.png)

### bind

`bind` is actually implemented via `ioctl`. Our `ioctl` supports the following commands:

```
SOCK_IOC_BIND           // bind address
SOCK_IOC_SHUTDOWN       // half-close
SOCK_IOC_SETSOCKOPT     // set options
SOCK_IOC_GETSOCKOPT     // get options
SOCK_IOC_GETSOCKNAME    // get local address
SOCK_IOC_GETPEERNAME    // get peer address
```

Today we'll implement `SOCK_IOC_BIND`. This command accepts a `sockaddr` structure. The code is straightforward:

```cpp
struct sockaddr {
    char addr[32];
    uint16_t port;
};

int tcp_bind(socket& sock, sockaddr* bind_conf) {
    strcpy(sock.src_addr, bind_conf->addr);
    sock.src_port = bind_conf->port;
    return 0;
}

int tcp_ioctl(socket& sock, const char* cmd, void* arg) {
    if (strcmp(cmd, "SOCK_IOC_BIND") == 0) {
        return tcp_bind(sock, reinterpret_cast<sockaddr*>(arg));
    }
    return -1;
}
```

Nothing special.

### listen

`listen` essentially implements the passive response we skipped in the previous section:

```cpp
    if (itr == map_to_sock.end()) { // not in existing connections
        printf("discard."); // todo: could be a passive connection, we discard for now
        return;
    }
```

We actually have this scenario: we just open a socket, register it in a "passive connection table", and active TCP connections will select a socket based on some strategy to establish the connection.

```cpp
while (client_fd = accept(conn, nullptr, nullptr))
```

Looking at this line from `tcp_server`, it's blocking. If our socket is selected, `accept` returns a new socket representing the connection to this host.

Going back to our `listen` implementation — it's essentially completing the registration operation. Register into what? A hash table again, but this time we use a binary tuple hash:

```cpp
struct sockaddr_hasher {
    size_t operator()(const sockaddr& q) const {
        size_t seed = 0;
        auto combine = [&](uint32_t v) {
            // classic bit perturbation algorithm to prevent hash collisions
            seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };
        combine(q.addr);
        combine(q.port);
        return seed;
    }
};

std::unordered_map<sockaddr, socket*, sockaddr_hasher> map_sockaddr_to_sock;

int tcp_listen(socket& sock, size_t queue_length) {
    SpinlockGuard guard(sock.lock);
    TCB* tcb = (TCB*)sock.data;
    if (tcb->state == tcb_state::LISTEN) {
        return -1;
    }
    tcb->state = tcb_state::LISTEN;
    tcb->accepted_queue_size = queue_length;

    sockaddr config;
    config.addr = sock.src_addr;
    config.port = sock.src_port;
    map_sockaddr_to_sock[config] = &sock;

    return -1;
}
```

Also, `listen` defines the length of the accept queue, which we need to record.

### accept

The logic of `accept` is to take a TCB that has already established a connection from the queue and return it. If the queue is empty, it waits.

However, this function is special because it creates a new fd. Let's think about the call chain: `accept()` → `sys_accept()` → `v_accept()` → `sockfs_accept()` → `tcp_accept()`. The return path would be: `tcp_accept()` returns a new TCB → `sockfs_accept()` wraps it into a socket, returns inode_id → `v_accept()` wraps it into an fd, then the user gets a new fd!

```cpp
TCB* tcp_accept(socket& sock, sockaddr* peeraddr, size_t* size) {
    uint32_t flags = spinlock_acquire(&(sock.lock));
    TCB* tcb = (TCB*)sock.data;
    if (tcb->state != tcb_state::LISTEN) {
        spinlock_release(&(sock.lock), flags);
        return nullptr;
    }
    TCB* ret = nullptr;
    if (!tcb->accepted_queue.empty()) {
        ret = tcb->accepted_queue.front();
        tcb->accepted_queue.pop_into(ret);
    } else {
        {
            SpinlockGuard guard(process_list_lock);
            process_list[cur_process_id]->state = process_state::WAITING;
            insert_into_process_queue(sock.wait_queue, process_list[cur_process_id]);
        }
        spinlock_release(&(sock.lock), flags);
        yield();
        flags = spinlock_acquire(&(sock.lock));
        if (!tcb->accepted_queue.empty()) {
            ret = tcb->accepted_queue.front();
            tcb->accepted_queue.pop_into(ret);
        }
    }
    spinlock_release(&(sock.lock), flags);
    return ret;
}
```

Wrap it layer by layer, preparing what can be prepared at each level so errors occur as early as possible!

```cpp
int accept(mounting_point* mp, uint32_t inode_id, sockaddr* peeraddr, size_t* size) {
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    socket& sock = data->sock[inode_id];
    if (sock.ptcl == protocol::TCP) {
        // prepare the wrapping inode
        uint32_t new_sock_num = init_new_socket(data);
        socket& new_sock = data->sock[new_sock_num];
        if (new_sock_num == 0) { // max sockets reached
            return -1;
        }
        new_sock.data = tcp_accept(sock, peeraddr, size);
        if (new_sock.data == nullptr) {
            new_sock.valid = 0;
            return -1;
        }
        return inode_id;
    }
    return -1;
}
```

```cpp
int v_accept(PCB* proc, int fd_pos, sockaddr* peeraddr, size_t* size) {
    SpinlockGuard guard(vfs_lock);
    if (fd_pos < 0 || fd_pos >= MAX_FD_NUM) return -1;
    file_description*& fd = proc->fd[fd_pos];
    if (!fd) return -1;
    mounting_point* mp = fd->mp;
    if (!mp || !(mp->operations->sock_opr)) return -1;

    // first prepare the new fd
    int fd_pos = alloc_fd_for_proc(proc);
    if (fd_pos == -1) {
        return -1;
    }
    int handle_id = get_empty_handle();
    if (handle_id == -1) {
        return -1;
    }
    uint32_t inode_id = mp->operations->sock_opr->accept(mp, fd->inode_id, peeraddr, size);
    if (inode_id == -1) {
        return -1;
    }
    file_description*& fd = proc->fd[fd_pos];
    fd = file_handle[handle_id] = (file_description*)kmalloc(sizeof(file_description));
    fd->mp = mp;
    fd->handle_id = handle_id;
    fd->inode_id = inode_id;
    fd->mode = O_RDWR;
    fd->offset = 0;
    fd->refcnt = 1;
    strcpy(fd->path, "/sock/tcp");
    ++file_handle_num;
    return proc->fd_num++;
}
```

### tcp_handler

```cpp
    if (itr == map_quad_to_sock.end()) { // not in existing connections
        printf("discard."); // todo: could be a passive connection, we discard for now
        return;
    }
```

Now we need to look up in our binary-tuple hash table to see if there's a socket listening:

```cpp
        sockaddr tofind_addr;
        tofind_addr.addr = dst_ip;
        tofind_addr.addr = dst_port;
        auto itr = map_sockaddr_to_sock.find(tofind_addr);
        if (itr == map_sockaddr_to_sock.end()) {
            // if not found by specific IP, try 0.0.0.0
            tofind_addr.addr = 0;
            itr = map_sockaddr_to_sock.find(tofind_addr);
            if (itr == map_sockaddr_to_sock.end()) { return; } // no matching socket, discard
        }
        socket* sock = itr->second;
```

What comes next? Naturally, responding with SYNACK. But doing this leads to a tricky situation:

```cpp
        send_tcp_pack(*sock, ((uint8_t)tcp_flags::SYN | (uint8_t)tcp_flags::ACK), nullptr, 0); // ???
```

Note the `*sock` here — it's the socket we're using to listen.

What was our intended flow? Since we've found a socket that can handle our new connection, we respond with SYNACK, create a new TCB, put it in the accept queue, wake up `Accept` to wrap it layer by layer into socket → fd, then hand it to the user. But right now, we need to respond with SYNACK first, and we don't even have our own socket yet!

Why does this happen? Because to call `send_tcp_pack`, we need the source and destination IP addresses and ports (the quadruple), along with the corresponding SEQ and ACK numbers. The latter we have, but the former is in the socket — so we end up in this awkward situation.

It seems we'll have to create the TCB first, then respond with SYNACK only after `accept`.

Wait... I realized I made a mistake: I shouldn't create the fd during the second handshake! This is wrong because the three-way handshake isn't complete yet, and the connection state is incorrect.

But now we definitely need to respond with SYNACK first, and we're back to square one. What to do? It seems we'll have to modify the function signature of `send_tcp_pack`. But `send_tcp_pack` needs to modify `tcb->seq` — without a TCB, how can it do that?... So I'll have to pass a TCB. But the TCB doesn't have the quadruple... So I need to put redundant source and destination IP addresses and ports in the TCB; but having them in two places means maintaining two copies, which I'm worried will cause issues. What a dilemma!

I've decided:

1. Keep the quadruple in only one place — in the TCB;
2. For ICMP scenarios that need IP addresses, get them from `socket->data`;
3. Make `socket->data` a union!

```cpp
typedef struct {
    uint8_t valid;
    protocol ptcl;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint16_t src_port;
    uint16_t dst_port;
    union {
        struct { icmp_info info; } icmp;
        struct { TCB* block; }        tcp;
    } data;
    spinlock lock;
    process_queue wait_queue;
} socket; // all in network byte order!
```

Clean!

```powershell
$ip = "10.0.1.1"
$port = 8080

$client = [System.Net.Sockets.TcpClient]::new($ip, $port)
$stream = $client.GetStream()
$reader = [System.IO.StreamReader]::new($stream)
$writer = [System.IO.StreamWriter]::new($stream)
$writer.AutoFlush = $true

Write-Host "Connected to ${ip}:${port}, type input to send, type exit to quit"

while ($true) {
    # Read messages from peer
    while ($stream.DataAvailable) {
        $msg = $reader.ReadLine()
        Write-Host "Peer: $msg"
    }

    # Check keyboard input
    if ([Console]::KeyAvailable) {
        $input = Read-Host
        if ($input -eq "exit") { break }
        $writer.WriteLine($input)
    }

    Start-Sleep -Milliseconds 100
}

$reader.Close()
$writer.Close()
$client.Close()
Write-Host "Disconnected"
```

I spent a long time troubleshooting a bug... If I can't find ARP information, I send an ARP request and sleep for 500ms before retrying. The problem is that since interrupts are disabled, I can't wake up...

If I ping first, the ARP cache has the MAC, and everything works fine. Sigh...

![image-20260311003519490](../assets/自制操作系统（25）：TCP（二）——三次握手（被动）/image-20260311003519490.png)

Added a buffer handler and continued to see what happens after sending SYNACK.

After sending SYNACK, we write it into the quadruple hash table. But we only have a bare TCB, so we need to change the hash table values to TCB, and record in the TCB which socket is mine and which socket is listening for me!

![image-20260311023148404](../assets/自制操作系统（25）：TCP（二）——三次握手（被动）/image-20260311023148404.png)

And now our `accept` has a return value!

---

Today we finally completed the three-way handshake on both sides! However, we still can't send or receive data. In the next section, we'll implement data transmission!
