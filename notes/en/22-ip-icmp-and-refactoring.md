## Homegrown OS (22): IP, ICMP and Refactoring

In the previous chapter, we implemented the RTL8139 NIC driver and used it to support the ARP protocol — our OS now has some degree of networking capability! However, our communication has been limited to the local LAN. Adapting the IP protocol allows us to break free from LAN boundaries for data transmission. Today, we'll adapt the IP protocol, then build ICMP on top of IP, and implement a user program — PING.

### IP

Implementing the IP protocol allows us to transmit data beyond the local LAN. Since IP is part of the network stack, it has its own packet format:

![](../assets/自制操作系统（22）：IP、ICMP与重构/9998227216184861-7f45a126-31a7-42ba-bac0-356a9ffbe9c8-image_task_01KK12BSEJT6F1N5RKG8R6FG6M.jpg)

There are many fields here, but I won't explain each one. There's plenty of information online. When researching, we can ignore the "Differentiated Services" and "Options" fields, as these are used by routing/forwarding devices — as an end device, we don't need to worry about them. (Of course, if you're implementing routing in your OS, that's a different story.)

#### IP Handler Registration

First, let's write IP handling logic inside the ethernet frame handler:

```cpp
void ip_handler(char* buffer, uint16_t size);

void ethernet_handler(char* buffer, uint16_t size) {
    if (size < sizeof(ethernet_head)) return;
    char* type = reinterpret_cast<ethernet_head*>(buffer)->type;
    // Network uses big-endian, but we compare byte-by-byte so it's fine
    if (type[0] == 0x08 && type[1] == 0x06) { // ARP
        arp_handler(buffer + sizeof(ethernet_head), size - sizeof(ethernet_head));
    } else if (type[0] == 0x08 && type[1] == 0x0) { // IP
        ip_handler(buffer + sizeof(ethernet_head), size - sizeof(ethernet_head));
    }
    return;
}
```

Then add some printing to the IP handler:

```cpp
void ip_handler(char* buffer, uint16_t size) {
    printf("it works!");
}
```

![image-20260306163554561](../assets/自制操作系统（22）：IP、ICMP与重构/image-20260306163554561.png)

If we ping our IP from WSL, we can receive it.

#### IP Handler Implementation

Of course, we want more than just printing output. Let's parse the data with a struct:

```cpp
typedef struct {
    uint8_t header_len : 4;
    uint8_t version : 4; // Tricky: version and header_len make one byte, but big-endian means swapping positions
    uint8_t type_of_svc : 8;
    uint16_t total_len : 16;
    uint16_t id : 16;
    uint16_t flags_n_offset : 16;
    uint8_t ttl : 8;
    uint8_t protocol : 8;
    uint16_t checksum : 16;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed)) ip_header;
```

What should we do when we receive an IP frame?

First, check the checksum. Then, check if the destination IP matches our own, and if the version is IPv4. If any of these fail, we can discard the packet (also check `header_len` — if it's less than 5, discard it too).

If it's a valid IP frame for us, check if it's an IP fragment. If so, send it to the fragment reassembly logic (not implemented yet).

If it's not a fragment, check the protocol type and dispatch to the appropriate handler.

##### Checksum

The checksum calculation accumulates every 16 bits of the header, then takes the one's complement:

```cpp
uint16_t checksum(ip_header* header) {
    uint16_t size = header->header_len * 4; // header_len in 4-byte units
    uint32_t res = 0;
    for (int i = 0; i < size / 2; ++i) { // 16 bits at a time
        res += *((uint16_t*)header + i);
    }
    while (res >> 16) {
        res = (res & 0xFFFF) + (res >> 16); // fold carry
    }
    return ~(uint16_t)res;
}
```

A checksum of 0 means no problem.

```cpp
    if (checksum(header) != 0) {
        printf("checksum error!\n");
        return;
    }
```

We also need to check if the destination IP matches ours, if the protocol is IPv4, and if the header is too small:

```cpp
    if (!is_same_ip(reinterpret_cast<uint8_t*>(&(header->dst_ip)), my_ip)) {
        return;
    }
    if (header->version != 4) { // IPv4 only
        return;
    }
    if (header->header_len < 5) { // header too small
        return;
    }
```

##### Fragment Detection

The IP Flag field format:

```
           0     1      2
        +-----+------+------+
        |  0  |  DF  |  MF  |
        +-----+------+------+
```

- Bit 0: Reserved, must be 0.
- Bit 1: DF (Don't Fragment), 0 = can fragment, 1 = cannot fragment.
- Bit 2: MF (More Fragment), 0 = last fragment, 1 = more fragments follow.

We'll branch based on the DF bit; reassembly is not implemented yet.

```cpp
    uint8_t flag = (header->flags_n_offset >> 13);
    if ((flag & 0b010) != 0) {// Don't fragment bit is set → needs reassembly
        reassemble(header);
        return;
    }
```

After all checks pass, dispatch the payload by protocol:

```cpp
    if (header->protocol == 0x01) { // ICMP
        icmp_handler(buffer + (header->header_len * 4), ip_total_len - (header->header_len * 4));
    }
```

There's a gotcha: the size passed during dispatch might have zero-padding for alignment, so use `header->total_len` to calculate the payload size.

Write a stub handler. After running it and pinging, we can now detect ICMP packets:

![image-20260306175956726](../assets/自制操作系统（22）：IP、ICMP与重构/image-20260306175956726.png)

### ICMP

Since it makes more sense to complete the full pipeline — IP packet parsing → ICMP message → ICMP reply sending → IP sending — let's introduce ICMP first.

#### ICMP Packet

ICMP (Internet Control Message Protocol) is a core protocol in the TCP/IP protocol suite, primarily used for transmitting control messages and error reports between network devices. It operates at the network layer (OSI Layer 3) and is defined in RFC 792. It doesn't carry specific application data but assists IP as an error-reporting helper.

ICMP packet format:

![](../assets/自制操作系统（22）：IP、ICMP与重构/9998227207057491-36097648-07f0-42c9-8799-4b64f9f0fcd4-image_task_01KK1B2CANAMS4VJ8196535WG4.jpg)

#### Common ICMP Message Types

| Type | Name                    | Description              | Common Scenario           |
| ---- | ----------------------- | ------------------------ | ------------------------- |
| 0    | Echo Reply              | Reply to ping            | Response received by `ping` |
| 3    | Destination Unreachable | Packet cannot be delivered | Port not open, host not found |
| 5    | Redirect                | Router suggests a better path | Multiple gateways on LAN |
| 8    | Echo Request            | Initiate ping            | Packet sent by `ping`     |
| 11   | Time Exceeded           | TTL expired              | Core principle of `traceroute` |

##### Type 3 Common Codes

| Code | Meaning                        | Explanation                      |
| ---- | ------------------------------ | -------------------------------- |
| 0    | Network Unreachable            | Router doesn't know the target network |
| 1    | Host Unreachable               | Target host not found            |
| 3    | Port Unreachable               | No process listening on target port (common for UDP) |
| 4    | Fragmentation Needed but DF Set | Packet too large but can't fragment |

The ICMP packet format varies significantly depending on the message type (or rather, its fixed header is quite small...).

The above lists common message types that we'll eventually implement. For now, let's start with Type 0 (Echo Reply) and Type 8 (Echo Request).

#### Echo Reply & Request

![](../assets/自制操作系统（22）：IP、ICMP与重构/9998227206339348-28c7c977-18c8-441c-a4ce-ce21a165a6bd-image_task_01KK1BR27Z978Y2W970PGYEXC0.jpg)

This is the data format for Echo Reply & Echo Request. They share the same format; interestingly, the response to a request is identical except for the type and checksum. So when we receive a request, we just change the type and checksum and send it back:

```cpp
constexpr uint8_t ICMP_ECHO_REPLY = 0x0;
constexpr uint8_t ICMP_ECHO_REQUEST = 0x8;

void handle_echo_request(uint32_t src_ip, char* buffer, uint16_t size) {
    *reinterpret_cast<uint8_t*>(buffer) = ICMP_ECHO_REPLY;
    *reinterpret_cast<uint16_t*>(buffer + 2) = 0; // Clear checksum, then recalculate
    uint16_t chksum = checksum(buffer, size);
    *reinterpret_cast<uint16_t*>(buffer + 2) = chksum;
    send_ipv4(src_ip, IP_PROTOCOL_ICMP, buffer, size);
}

void icmp_handler(uint32_t src_ip, char* buffer, uint16_t size) {
    if (checksum(buffer, size) != 0) {
        return;
    }
    // Dispatch by type
    uint8_t type = *(reinterpret_cast<uint8_t*>(buffer));
    if (type == ICMP_ECHO_REQUEST) {
        handle_echo_request(src_ip, buffer, size);
    }
}
```

We need to implement `send_ipv4`. So let's go back to IP and finish it.

### Back to IP

#### send_ipv4 Function

Now our system can respond to pings! Congratulations!

![image-20260306213835235](../assets/自制操作系统（22）：IP、ICMP与重构/image-20260306213835235.png)

### Network Stack Refactoring

Unfortunately! Our network stack is starting to show signs of a big ball of mud!

We need to refactor from the bottom up...

#### Locks

In our RTL8139 driver, `send_buffer` and `rbuffer` can potentially be written to by multiple processes simultaneously... so we need to add locks to their access.

#### Timeout Mechanism

We need to add a timeout mechanism to `nic_write`:

```cpp
int nic_write(const char* buffer, uint32_t size) {
    if (size > SEND_BUFFER_SIZE || !is_initialized) return -1;

    SpinlockGuard guard(send_buffer_lock);

    uint32_t old_ticks = pit_get_ticks();
    while(!(inl(io_addr + REG_TSD[tx_cur]) & (1 << 13))) {
        if (pit_get_ticks() - old_ticks > 100) { // 1 second timeout
            return -1;
        }
```

#### IPADDR, MACADDR Encapsulation

IP and MAC addresses appear in too many forms throughout the code. It's time to give them unified encapsulation with utility functions:

```cpp
typedef struct macaddr {
    static constexpr uint64_t MASK = 0x0000FFFFFFFFFFFF; // Low 6 bytes valid

    uint64_t addr; // Stored in network byte order, only low 6 bytes valid

    macaddr(uint64_t input_addr = 0) : addr(input_addr & MASK) {}

    macaddr(const char* s) {
        addr = 0;
        memcpy(&addr, s, 6);
    }

    macaddr(const uint8_t* s) {
        addr = 0;
        memcpy(&addr, s, 6);
    }

    macaddr(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f) {
        addr = 0;
        uint8_t octets[6] = {a, b, c, d, e, f};
        memcpy(&addr, octets, 6);
    }

    bool operator==(const macaddr& rhs) const {
        return addr == rhs.addr;
    }

    bool operator!=(const macaddr& rhs) const {
        return addr != rhs.addr;
    }

    bool operator==(const uint8_t* rhs) const {
        return memcmp(&addr, rhs, 6) == 0;
    }

    bool operator!=(const uint8_t* rhs) const {
        return memcmp(&addr, rhs, 6) != 0;
    }

    friend bool operator==(const uint8_t* lhs, const macaddr& rhs) {
        return rhs == lhs;
    }

    friend bool operator!=(const uint8_t* lhs, const macaddr& rhs) {
        return rhs != lhs;
    }

    explicit operator uint64_t() const {
        return addr;
    }

    macaddr& operator=(const char* s) {
        addr = 0;
        memcpy(&addr, s, 6);
        return *this;
    }

    macaddr& operator=(const uint8_t* s) {
        addr = 0;
        memcpy(&addr, s, 6);
        return *this;
    }

    macaddr& operator=(uint64_t u) {
        addr = u & MASK;
        return *this;
    }

    void to_bytes(uint8_t out[6]) const {
        memcpy(out, &addr, 6);
    }
} macaddr;

struct ipv4addr {
    uint32_t addr = 0; // Network byte order

    ipv4addr() = default;
    ipv4addr(const ipv4addr&) = default;

    explicit ipv4addr(uint32_t raw) : addr(raw) {}

    explicit ipv4addr(const char* s) {
        const uint8_t* u = reinterpret_cast<const uint8_t*>(s);
        memcpy(&addr, u, 4);
    }

    explicit ipv4addr(const uint8_t* u) {
        memcpy(&addr, u, 4);
    }

    explicit ipv4addr(const char* a, const char* b,
                      const char* c, const char* d) {
        uint8_t octets[4] = {
            (uint8_t)atoi(a), (uint8_t)atoi(b),
            (uint8_t)atoi(c), (uint8_t)atoi(d)
        };
        memcpy(&addr, octets, 4);
    }

    ipv4addr& operator=(const ipv4addr&) = default;

    ipv4addr& operator=(const uint8_t* u) {
        memcpy(&addr, u, 4);
        return *this;
    }

    ipv4addr& operator=(const char* s) {
        const uint8_t* u = reinterpret_cast<const uint8_t*>(s);
        memcpy(&addr, u, 4);
        return *this;
    }

    bool operator==(const ipv4addr& rhs) const { return addr == rhs.addr; }
    bool operator!=(const ipv4addr& rhs) const { return addr != rhs.addr; }

    bool operator==(const uint8_t* rhs) const { return memcmp(&addr, rhs, 4) == 0; }
    bool operator!=(const uint8_t* rhs) const { return memcmp(&addr, rhs, 4) != 0; }

    explicit operator uint32_t() const { return addr; }

    void to_bytes(uint8_t out[4]) const { memcpy(out, &addr, 4); }
};
```

#### ARP Wait Queue

We previously encountered a situation where, when sending an IP packet, we couldn't find the ARP entry and had to give up. We need to add a wait-and-retry mechanism with timeout to ARP:

```cpp
bool arp_table_lookup(const ipv4addr& ip, macaddr& mac) {
    if (ip_to_mac.find((uint32_t)ip) == ip_to_mac.end()) {
        send_arp(htons(APR_OPCODE_REQ), broadcast_mac, ipv4addr(ip));
        ...what to do?
        return false;
    }
    mac = ip_to_mac[(uint32_t)ip];
    return true;
}
```

##### Timeout Mechanism

Let's implement a timeout mechanism first. Since we don't have a timer yet, we need to build one quickly:

```cpp
#include <kernel/timer.hpp>
#include <kernel/mm.hpp>

#include <priority_queue>

constexpr uint16_t MAX_TIMER_NUM = 256;
typedef struct  {
    uint32_t wake_tick;
    timer_callback_func callback_func;
    void* args;
} timer_config;

struct TimerCmp {
    bool operator()(const timer_config& a, const timer_config& b) const {
        return a.wake_tick > b.wake_tick;  // a > b → a has lower priority
    }
};

std::priority_queue<timer_config, MAX_TIMER_NUM, TimerCmp> pq;

bool register_timer(uint32_t ms_10, timer_callback_func callback, void* args) {
    if (pq.size() >= MAX_TIMER_NUM) {
        return false;
    }
    pq.emplace(timer_config{ ms_10, callback, args });
    return true;
}

void timer_handler(uint32_t current_tick) {
    if (!pq.empty() && pq.top().wake_tick <= current_tick) {
        timer_config t;
        pq.pop_into(t);
        t.callback_func(t.args); // ??
    }
}
```

I used a priority queue for a more efficient timer. Notice the `??` comment above. At first, I planned to trigger the callback directly. But then I thought: if there are multiple processes and process 1 triggers process 2's timer while executing, should process 1 execute process 2's callback? The answer is no — and it's dangerous, because the code is unknown and could potentially come from userspace.

A better approach is to create a dedicated kernel process `ktimerd` to handle expired timer callbacks:

```cpp
void kernel_timer_main() {
    while(1) {
        while(!due_timer_queue.empty()) {
            timer_config conf = due_timer_queue.front();
            conf.callback_func(conf.args);
            due_timer_queue.pop();
        }
        yield();
    }
}

void init_kernel_timer() {
    create_process("ktimerd", (void *)(&kernel_timer_main), nullptr); // ktimerd process
}

void timer_handler(uint32_t current_tick) {
    if (!pq.empty() && pq.top().wake_tick <= current_tick) {
        due_timer_queue.push(pq.top());
        pq.pop();
    }
}
```

This means when ARP table lookup fails, we can register a timer, add ourselves back to the wait queue in the callback, suspend ourselves, and start the timer... Wait, why not just encapsulate a `sleep` function?

```cpp
void sleep(uint32_t ms) {
    uint32_t ms_10 = (ms + 9) / 10;
    pid_t sleep_pid;
    uint32_t flags = spinlock_acquire(&process_list_lock);
    auto callback = [](void* args) -> void {
        pid_t* pid = (pid_t*)args;

        uint32_t flags = spinlock_acquire(&process_list_lock);
        process_list[*pid]->state = process_state::READY;
        spinlock_release(&process_list_lock, flags);
        
        insert_into_scheduling_queue(*pid);
        return;
    };
    sleep_pid = cur_process_id;
    register_timer(ms_10, callback, &sleep_pid);
    process_list[cur_process_id]->state = process_state::SLEEPING;
    spinlock_release(&process_list_lock, flags);
    yield();
}
```

Now we can implement the retry mechanism like this:

```cpp
bool arp_table_lookup(const ipv4addr& ip, macaddr& mac) {
    for (int attempt = 0; attempt <= 3; ++attempt) {
        {
            SpinlockGuard guard(ip_to_mac_lock);
            auto itr = ip_to_mac.find((uint32_t)ip);
            if (itr != ip_to_mac.end()) {
                mac = itr->second;
                return true;
            }
        }
        if (attempt == 3) break;
        send_arp(htons(APR_OPCODE_REQ), broadcast_mac, ipv4addr(ip));
        sleep(500);
    }
    return false;
}
```

---

I had planned to implement ping in this section, but considering that the prerequisites for user-mode ICMP calls aren't ready yet, we'll defer it to the next chapter.
