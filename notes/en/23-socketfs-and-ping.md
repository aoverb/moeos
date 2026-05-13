## Homemade Operating System (23): SocketFS and the Ping Application

Today we're going to expose part of our network stack's interface to user mode through SocketFS.

### Target State

Although we only have ICMP in mind right now, building SocketFS with such a narrow view will inevitably lead to early design decisions that will be troublesome for our future selves. But this is exactly the process of understanding what "correct design" means! If someone handed us a standard right now and told us to just follow it, we might accept the standard but never understand why it's done that way. So I think it's necessary to build something our own way first and optimize based on our needs later. So, top-down, let's first create a user-mode `ping`:

```cpp
#include <file.h>
#include <stdio.h>
#include <string.h>
#include <format.h>
#include <stdlib.h>
#include <time.h>

struct icmp_recv_result {
    uint8_t  src_ip[4];
    uint8_t  ttl;
    uint16_t seq;
    uint32_t rtt_ms;
    // Followed by payload
};

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: ping <ip addr>\n");
        return 0;
    }
    
    char ip_addr[16];
    snprintf(ip_addr, sizeof(ip_addr), "%s", argv[1]);

    char icmp_open_path[64];
    snprintf(icmp_open_path, sizeof(icmp_open_path), "/sock/%s/icmp/", ip_addr);

    int file = open(icmp_open_path, O_RDWR);
    if (file == -1) {
        printf("icmp unsupported!\n");
        return 0;
    }

    const char* payload = "1234567890loliOS$!@&#($&)OWOXCVBNMASDFGHJKLQWERTYUIOPBA";

    printf("PING %s %d bytes of data.\n", ip_addr, strlen(payload));
    int ping_count = 5;
    char* buffer = (char*)malloc(sizeof(char) * 2048);
    while(ping_count--) {
        
        clock_t ticks = clock();

        uint32_t recv_size = 0;
        if (!write(file, payload, strlen(payload))) {
            printf("Failed to send icmp request.\n");
        } else if ((recv_size = read(file, buffer, 2048))){

            printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%dms\n", recv_size, ip_addr,
                reinterpret_cast<icmp_recv_result*>(buffer)->seq,
                reinterpret_cast<icmp_recv_result*>(buffer)->ttl,
                (clock() - ticks) * 10);
        } else {
            printf("Request timed out.\n")
        }
        
        sleep(1);
    }
    free(buffer);
    return 0;
}
```

Note: the `sleep` and `clock` functions aren't implemented yet. Our first milestone is to get this program to compile, so I'll implement these two functions first.

### Level 1: Getting Past Compilation

```cpp
// SLEEP(ebx = ms)
int sys_sleep(interrupt_frame* reg) {
    uint32_t* fds = reinterpret_cast<uint32_t*>(reg->ebx);
    sleep(*fds * 100);
    return 0;
}

// CLOCK(eax = ticks)
int sys_clock(interrupt_frame* reg) {
    return pit_get_ticks();
}
```

Define two syscalls. Also add the related calls to libc.

```cpp
int sys_sleep(interrupt_frame* reg) {
    uint32_t fds = reinterpret_cast<uint32_t>(reg->ebx);
    sleep(fds * 1000);
    return 0;
}

static uint32_t clock() {
    return syscall0((uint32_t)SYSCALL::CLOCK);
}
```

#### Pitfall: Kernel Process Disables Interrupts

```assembly
process_switch_to:
    cli
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ebp

    #save...
    movl cur_process_id, %eax
    movl process_list(, %eax, 4), %eax
    movl %esp, 4(%eax)

    #update cur_process_id and esp registers...
    movzbl 20(%esp), %eax
    movl process_list(, %eax, 4), %eax
    movl (%eax), %ebx
    cmp %ebx, (cur_process_id)
    je 1f
    movl %ebx, cur_process_id
    movl 4(%eax), %esp

    movl 8(%eax), %eax
    movl %cr3, %ebx
    cmpl %ebx, %eax
    je 1f
    movl %eax, %cr3
1:
    popl %ebp
    popl %edi
    popl %esi
    popl %ebx
    ret
```

Kernel processes disable interrupts but never re-enable them — our timer is broken.

```cpp
    *((uintptr_t*)(new_process->esp - 16)) = 0x202;
    *((uintptr_t*)(new_process->esp - 20)) = reinterpret_cast<uintptr_t>(schedule_tail_restore);
    *((uintptr_t*)(new_process->esp - 24)) = 0;  // ebx
    *((uintptr_t*)(new_process->esp - 28)) = 0;  // esi
    *((uintptr_t*)(new_process->esp - 32)) = 0;  // edi
    *((uintptr_t*)(new_process->esp - 36)) = 0;  // ebp
```

When constructing a new process's kernel stack, we just need to add an extra function dedicated to restoring `eflags` (rather than jumping directly to the entry).

![image-20260308150453114](../assets/自制操作系统（23）：SocketFS与Ping应用程序/image-20260308150453114.png)

After fixing this bug, our program no longer freezes as soon as it enters `sleep`, and we get output (albeit fake data).

### Level 2: Mounting SocketFS

We now need to solve the problem of failing to open this "file".

```cpp
/sock/%s/icmp
```

We need to mount SocketFS at `/sock` and have it route to our pseudo file system... But what is a Socket?

#### A Simple Socket Definition

**Warning: The following socket definition is optimized for convenience, and definitely differs from the formal socket definition.**

We can think of a Socket as a communication port. We specify the protocol and address used by this port, use `open` to create a file descriptor pointing to a socket, and then use `read` and `write` to read and write data to/from the port this descriptor points to.

#### Socket Organization in SocketFS

```cpp
typedef struct {
    protocol ptcl;
    char addr[64];
} socket;

typedef struct {
    socket* sock[MAX_SOCK_NUM];
    uint32_t sock_num;
} socketfs_data;
```

A simple definition for now. It's essentially an array of sockets, each recording the protocol used and the corresponding address.

#### Mount

Create a root node.

```cpp
static int mount(mounting_point* mp) {
    if (!mp) return -1;
    mp->data = (socketfs_data*)kmalloc(sizeof(socketfs_data));
    memset(mp->data, 0, sizeof(socketfs_data));
    reinterpret_cast<socketfs_data*>(mp->data)->sock[0].ptcl = protocol::ROOT;
    strcpy(reinterpret_cast<socketfs_data*>(mp->data)->sock[0].addr, ".");
    reinterpret_cast<socketfs_data*>(mp->data)->sock[0].addr;
    reinterpret_cast<socketfs_data*>(mp->data)->sock_num = 1;
    return 0;
}
```

#### Open

We need to parse strings like `/sock/<addr>/<protocol>`. For convenience, we ask Claude to help us write a `vsscanf` function.

```cpp
static int open(mounting_point* mp, const char* path,  uint8_t mode) {
    ++path; // First character is a slash
    if (!mp->data) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    if (mode == O_CREATE) { // Create a socket
        uint32_t new_sock_num = 0;
        for (int i = 0; i < MAX_SOCK_NUM; ++i) {
            if (data->sock[i].valid == 0) {
                new_sock_num = i;
                break;
            }
        }
        if (new_sock_num == 0) { // Socket count reached maximum
            return -1;
        }
        socket& new_sock = data->sock[new_sock_num];
        new_sock.valid = 1;

        // Socket format: <addr>/<protocol>
        char protocol[8];
        protocol[0] = '\0';
        sscanf_s(path, "%[^/]/%[^/]", new_sock.addr, sizeof(new_sock.addr),
                                      protocol, sizeof(protocol));
        if (!strlen(new_sock.addr) || !strlen(protocol)) {
            return -1;
        }

        if (strcmp("icmp", protocol) == 0) {
            new_sock.ptcl = protocol::ICMP;
        } else {
            // Unsupported protocol
            return -1;
        }
        return new_sock_num;
    } else { // Open existing socket
        return -1; // todo: not supported yet
    }
    return -1;
}
```

When the user calls the open interface with create mode, it mainly parses the path to check if the protocol and address are valid. For now, only the protocol is validated; the address will be validated later when the corresponding read/write interface is called.

#### write

For the write interface, we simply map the corresponding parameters to the specified protocol's send interface.

![](../assets/自制操作系统（23）：SocketFS与Ping应用程序/9998227207057491-36097648-07f0-42c9-8799-4b64f9f0fcd4-image_task_01KK1B2CANAMS4VJ8196535WG4_1.jpg)

For ICMP, we let the user construct the entire ICMP packet and pass it in.

But the ICMP handler we implemented in the previous section doesn't have a `send` function yet. Let's go implement one.

##### icmp_send

When I was determining the target state earlier, I didn't consider the variability of ICMP packet formats. I should let the user construct the entire ICMP packet! This means we need to change how the payload is constructed in the ping program code — but that's a matter for later.

This is the `icmp_send` implementation I initially thought of:

```cpp
void icmp_send(const ipv4addr& dst_ip, char* buffer, uint16_t size) {
    send_ipv4(dst_ip, IP_PROTOCOL_ICMP, buffer, size);
}
```

At first glance, it works. But thinking more carefully — when an ICMP REPLY packet comes back, how does it know which REQUEST this packet corresponds to? Oh, the IP header has an ID field. Maybe we could fill the ID field with the INODE ID and send it out, then check the ID field when it comes back. But does the replying party guarantee that the ID will be returned as-is? Probably not, because when I construct a REPLY, I just fill the IP header ID with 0, and the other side can still respond to my REPLY. Is there another way?

Then I realized — Echo and Reply also have an ID field! = =;

![](../assets/自制操作系统（23）：SocketFS与Ping应用程序/9998227206339348-28c7c977-18c8-441c-a4ce-ce21a165a6bd-image_task_01KK1BR27Z978Y2W970PGYEXC0_1.jpg)

This is the data format for Echo Reply & Echo Request. It was in the previous section too — I completely forgot...

But looking at it this way, since our ICMP packets are transparently passed through with user construction, the Identifier would probably need to be filled by the user. If the user fills it in wrong, the reply could be sent to another program — that doesn't seem reliable. Perhaps we should take over this field.

```cpp
int icmp_send_withid(const ipv4addr& dst_ip, uint16_t id, char* buffer, uint16_t size) {
    // Even though our current maximum socket count is 1024, in this case the identifier should be fine.
    // But I'm not ruling out the possibility of increasing this number later.
    // I don't want to forget to update this when increasing the socket count, causing identifier conflicts,
    // so I'll add an assertion here...
    // Even though this assertion will never fail for now, who knows? Maybe later I'll change MAX_SOCK_NUM's type.
    static_assert(MAX_SOCK_NUM <= 65536);
    *reinterpret_cast<uint8_t*>(buffer + 4) = id;
    return send_ipv4(dst_ip, IP_PROTOCOL_ICMP, buffer, size);
}

int icmp_send(const ipv4addr& dst_ip, const char* buffer, uint16_t size) {
    return send_ipv4(dst_ip, IP_PROTOCOL_ICMP, buffer, size);
}
```

But in reality, sometimes we might want this field to be passed through as-is, suggesting we might need an additional way to input packets. So I've reserved `icmp_send` here. For now, we'll default to the system-processed route.

```cpp
static int write(mounting_point* mp, uint32_t inode_id, const char* buffer, uint32_t size) {
    if (!mp->data || (MAX_SOCK_NUM <= inode_id)) return -1;
    socketfs_data* data = (socketfs_data*)mp->data;
    if (data->sock[inode_id].valid == 0) return -1;
    socket& cur_sock = data->sock[inode_id];
    uint8_t trans_addr[4];
    sscanf_s(cur_sock.addr, "%d.%d.%d.%d", trans_addr[0], trans_addr[1], trans_addr[2], trans_addr[3]);
    ipv4addr addr = ipv4addr(trans_addr);

    char* id_modified_buffer = (char*)kmalloc(size);
    memcpy(id_modified_buffer, buffer, size);
    int ret = icmp_send_withid(addr, inode_id, id_modified_buffer, size);
    kfree(id_modified_buffer);
    return ret;
}
```

We need to separately copy a buffer for `icmp_send_withid` to modify.

##### Simple Test

![image-20260308184629028](../assets/自制操作系统（23）：SocketFS与Ping应用程序/image-20260308184629028.png)

Looking at the tcpdump results, our payload has been sent out, but since we haven't correctly constructed the packet yet, we don't receive a REPLY.

#### Completing the Payload

```cpp
    const char* data_str = "1234567890loliOS$!@&#($&)OWOXCVBNMASDFGHJKLQWERTYUIOPBA";

    size_t payload_size = sizeof(icmp_echo_head) + sizeof(char) * (strlen(data_str) + 1);
    void* payload = malloc(payload_size);
    icmp_echo_head* head = (icmp_echo_head*)payload;
    memcpy(payload + sizeof(icmp_echo_head), data_str, payload_size - sizeof(icmp_echo_head));
    head->type = ICMP_ECHO_REQUEST;
    head->code = 0;
```

![image-20260308202723188](../assets/自制操作系统（23）：SocketFS与Ping应用程序/image-20260308202723188.png)

##### Common ICMP Message Types

| Type | Name                    | Description                  | Common Scenarios            |
| ---- | ----------------------- | ---------------------------- | --------------------------- |
| 0    | Echo Reply              | Reply to ping                | Received `ping` responses   |
| 3    | Destination Unreachable | Packet cannot be delivered   | Port not open, host not found |
| 5    | Redirect                | Router suggests better route | Multiple gateways on LAN    |
| 8    | Echo Request            | Initiate ping                | Outgoing `ping` packets     |
| 11   | Time Exceeded           | TTL expired                  | Core principle of `traceroute` |

##### Common Type 3 Codes

| Code | Meaning                          | Plain Explanation                          |
| ---- | -------------------------------- | ------------------------------------------ |
| 0    | Network Unreachable              | Router doesn't know how to reach network   |
| 1    | Host Unreachable                 | Target host not found                      |
| 3    | Port Unreachable                 | No process listening on target port (UDP)  |
| 4    | Fragmentation Needed but DF Set  | Packet too large and fragmentation disabled |

We need to finish implementing the message type handling that was left incomplete in the previous section.

#### read

The `read` function must work as follows:

First, `read` calls `icmp_read`. `icmp_read` checks if the resource is available. If not, it suspends the current process. The `icmp_handler`, based on the ID (i.e., `inode_id`) in the returned packet, pushes the packet into the corresponding queue.

Ultimately, we solve this by embedding a queue inside the socket.

### Level 3: ping

![image-20260309003059861](../assets/自制操作系统（23）：SocketFS与Ping应用程序/image-20260309003059861.png)

### Level 4: Bonus Level — PING External Network

To ping external networks:

```cpp
    // Look up ARP table
    macaddr dst_mac;
    ipv4addr dst_ip_x;
    if ((ntohl(dst_ip.addr) & ntohl(getLocalNetconf()->mask.addr)) ==
    (ntohl(getLocalNetconf()->ip.addr) & ntohl(getLocalNetconf()->mask.addr))) { // Same subnet
        dst_ip_x = ipv4addr(head->dst_ip);
    } else {
        dst_ip_x = ipv4addr(getLocalNetconf()->gateway);
    }
```

Add a logic check: when looking up the MAC, if not on the same subnet, route to the gateway.

```shell
sudo sysctl -w net.ipv4.ip_forward=1
```

Also enable IP forwarding on Linux, so it will help us forward packets destined elsewhere.

```shell
sudo iptables -t nat -A POSTROUTING -s 10.0.1.0/24 -o eth0 -j MASQUERADE
sudo iptables -A FORWARD -i tap0 -o eth0 -j ACCEPT
sudo iptables -A FORWARD -i eth0 -o tap0 -m state --state RELATED,ESTABLISHED -j ACCEPT
```

![image-20260309003858352](../assets/自制操作系统（23）：SocketFS与Ping应用程序/image-20260309003858352.png)

Our OS can ping external networks!

### Timeout Mechanism

```cpp
        while (1) {
            {
                SpinlockGuard guard(cur_sock.lock);
                if (cur_sock.data) {
                    icmp_node* icmp_data = (icmp_node*)cur_sock.data;
                    size_t cpysize = icmp_data->size < size ? icmp_data->size : size;
                    memcpy(buffer, icmp_data->data, cpysize);
                    icmp_node* next = icmp_data->next;
                    kfree(icmp_data->data);
                    kfree(cur_sock.data);
                    cur_sock.data = next;
                    ret = cpysize;
                    break;
                } else {
                    SpinlockGuard guard(process_list_lock);
                    process_list[cur_process_id]->state = process_state::WAITING;
                    insert_into_process_queue(cur_sock.wait_queue, process_list[cur_process_id]);
                }
            }
            yield();
        }
```

Adding a timer to this code is no easy task...

Add a timeout logic:

```cpp
void timeout(process_queue* queue, uint32_t ms) {
    uint32_t ms_10 = (ms + 9) / 10;
    uint32_t flags = spinlock_acquire(&process_list_lock);
    auto callback = [](pid_t pid, void* queue) -> void {
        uint32_t flags = spinlock_acquire(&process_list_lock);
        if (!process_list[pid] ||
            process_list[pid]->state != process_state::WAITING) {
            spinlock_release(&process_list_lock, flags);
            return;
        }
        process_list[pid]->state = process_state::READY;
        process_queue& pq = *((process_queue*)queue);
        remove_from_process_queue(pq, pid);
        spinlock_release(&process_list_lock, flags);
        insert_into_scheduling_queue(pid);
    };
    timer_id_t id = register_timer(pit_get_ticks() + ms_10, callback, queue);
    process_list[cur_process_id]->state = process_state::WAITING;
    spinlock_release(&process_list_lock, flags);
    yield();
    cancel_timer(id);
}
```

The core issue is that the waiting queue management information is not synchronized with the timer. So I simply pass the queue into the timer and let it be managed jointly with `icmp_handler`.

```cpp
{
            uint32_t flags = spinlock_acquire(&(cur_sock.lock));
            if (cur_sock.data) {
                icmp_node* icmp_data = (icmp_node*)cur_sock.data;
                size_t cpysize = icmp_data->size < size ? icmp_data->size : size;
                memcpy(buffer, icmp_data->data, cpysize);
                icmp_node* next = icmp_data->next;
                kfree(icmp_data->data);
                kfree(cur_sock.data);
                cur_sock.data = next;
                ret = cpysize;
            } else {
                {
                    SpinlockGuard guard(process_list_lock);
                    process_list[cur_process_id]->state = process_state::WAITING;
                    insert_into_process_queue(cur_sock.wait_queue, process_list[cur_process_id]);
                }
                spinlock_release(&(cur_sock.lock), flags);
                timeout(&(cur_sock.wait_queue), 1000);
                uint32_t flags = spinlock_acquire(&(cur_sock.lock));
                if (cur_sock.data) {
                    icmp_node* icmp_data = (icmp_node*)cur_sock.data;
                    size_t cpysize = icmp_data->size < size ? icmp_data->size : size;
                    memcpy(buffer, icmp_data->data, cpysize);
                    icmp_node* next = icmp_data->next;
                    kfree(icmp_data->data);
                    kfree(cur_sock.data);
                    cur_sock.data = next;
                    ret = cpysize;
                }
            }
            spinlock_release(&(cur_sock.lock), flags);
        }
```

With the `timeout` function, the mechanism becomes as shown above.

---

### Summary

Next step: TCP!
