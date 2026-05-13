## Homemade Operating System (26): TCP (Part 3) — Data Send and Receive

Today we'll implement TCP data transmission.

### User Mode

```cpp
    pollfd fds[2] = {
        { .fd = 0, .events = POLLIN, .revents = 0}, // stdin
        { .fd = conn, .events = POLLIN, .revents = 0 }
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
```

This is the remaining code we need to implement. A key concept stands out: `poll`.

#### poll

The concept of `poll` is simple: we give it some file descriptors, call it to block until some fd generates an event or timeout, then it lets us proceed with the flow below. We haven't implemented poll yet, so we need to provide its implementation.

One straightforward approach would be to call `read` on all file descriptors. But this is inefficient, and some reads are blocking.

We can imagine that `poll` would put the current process into the wait queues of these file descriptors, and the fd's driver (or something related) would be responsible for waking up processes in the wait queue. We could add another file operation function that registers a process into the fd's wait queue, letting the fd driver wake it up.

But our current process model only supports being in one wait queue. To be in multiple queues, we need another layer of abstraction, with locking when removing from wait queues, checking if the state is still WAITING. And that's not enough — we should be able to remove ourselves from all wait queues.

Maybe we can think differently. Instead of hanging ourselves on any fd's wait queue, we call the fd's callback function to check if there's data.

```cpp
int sys_poll(interrupt_frame* reg) {
    pollfd* fds         = reinterpret_cast<pollfd*>(reg->ebx);
    uint32_t fd_num     = reg->ecx;
    uint32_t timeout    = reg->edx;
    process_queue poll_queue;
    bool has_event = false;
    bool has_data = false;
    {
        SpinlockGuard guard(process_list_lock);
        PCB* cur_pcb = current_pcb();
        insert_into_process_queue(poll_queue, cur_pcb);
        for (int i = 0; i < fd_num; ++i) {
            if (cur_pcb->fd[fds[i].fd] == nullptr) {
                if (fds[i].events & INVFD) {
                    fds[i].revents |= INVFD;
                    has_event = true;
                }
            } else {
                int ret = v_peek(cur_pcb, fds[i].fd);
                if (ret < 0 && (fds[i].events & ERROR)) {
                    fds[i].revents |= ERROR;
                    has_event = true;
                } else if (ret == 0 && (fds[i].events & POLLIN)) {
                    fds[i].revents |= POLLIN;
                    has_event = true;
                    has_data = true;
                }
                v_setpoll(cur_pcb, fds[i].fd, &poll_queue);
            }
        }
        if (!has_event) {
            current_pcb()->state = process_state::WAITING;
        }
    }
    if (!has_event) {
        ::timeout(&poll_queue, timeout);
    } else {
        SpinlockGuard guard(process_list_lock);
        remove_from_process_queue(poll_queue, current_pcb()->pid);
        return 1;
    }
    {
        SpinlockGuard guard(process_list_lock);
        PCB* cur_pcb = current_pcb();
        for (int i = 0; i < fd_num; ++i) {
            if (cur_pcb->fd[fds[i].fd] == nullptr) {
                if (fds[i].events & INVFD) {
                    fds[i].revents |= INVFD;
                }
            } else {
                int ret = v_peek(cur_pcb, fds[i].fd);
                if (ret < 0 && (fds[i].events & ERROR)) {
                    fds[i].revents |= ERROR;
                } else if (ret == 0 && (fds[i].events & POLLIN)) {
                    fds[i].revents |= POLLIN;
                    has_data = true;
                }
                v_setpoll(cur_pcb, fds[i].fd, nullptr);
            }
        }
    }
    return has_data;
}
```

We ultimately went with this approach that doesn't support multi-process fd sharing: we put ourselves into a local wait queue and pass the head pointer of our queue to the fd's `poll_queue`. When data arrives, the fd is responsible for waking us up from the queue. Since everyone uses `process_list_lock` when removing from queues, there's no concern about being woken multiple times. After waking, we `peek` each file, record events, and finally clear everyone's `poll_queue`.

If we later need multi-process fd sharing, we'll make `poll_queue` a linked list, create our own list entries, acquire a shared lock stored in the fd's `poll_queue_lock` for insertion, and do the same for cleanup.

#### read

We haven't adapted the TCP read logic yet — because our window is still empty! But we can add this logic now:

```cpp
int tcp_read(socket& sock, char* buffer, uint32_t size) {
    // head and tail, left-inclusive right-exclusive
    TCB* tcb = sock.data.tcp.block;
    SpinlockGuard guard(tcb->lock);
    char* window = sock.data.tcp.block->window;
    size_t read_size;
    if (tcb->window_used_size == 0) {
        return 0;
    }
    if (tcb->window_head < tcb->window_tail) {
        read_size = tcb->window_tail - tcb->window_head < size?
                           tcb->window_tail - tcb->window_head : size;
        memcpy(buffer, window + tcb->window_head, read_size);
    } else {
        size_t first = tcb->window_size - tcb->window_head;
        if (size <= first) {
            memcpy(buffer, window + tcb->window_head, size);
            read_size = size;
        } else {
            memcpy(buffer, window + tcb->window_head, first);
            size_t second = (size - first) > tcb->window_tail ? tcb->window_tail : size - first;
            memcpy(buffer + first, window, second);
            read_size = first + second;
        }
    }
    tcb->window_head = (tcb->window_head + read_size) % tcb->window_size;
    tcb->window_used_size -= read_size;
    return read_size;
}
```

We added a `window_used_size` field to the TCB's window, making the circular buffer semantics clearer.

```cpp
    case tcb_state::ESTABLISHED:
    {
        if ((header->flags & (uint8_t)tcp_flags::ACK) == 0) {
            break;
        }
        if (ntohl(header->seq_num) != tcb->ack) { // simple implementation: drop out-of-order packets
            break;
        }
        size_t payload_size = size - ip_header_size - header->data_offset * 4; // don't forget to multiply by 4
        char* payload = buffer + header->data_offset * 4 + ip_header_size;
        if (tcb->window_size - tcb->window_used_size < payload_size) {
            break; // exceeds current window size, discard
        }
        if (tcb->window_tail < tcb->window_head) {
            memcpy(tcb->window + tcb->window_tail, payload, payload_size);
        } else {
            size_t first = tcb->window_size - tcb->window_tail;
            if (first >= payload_size) {
                memcpy(tcb->window + tcb->window_tail, payload, payload_size);
            } else {
                memcpy(tcb->window + tcb->window_tail, payload, first);
                size_t second = payload_size - first;
                memcpy(tcb->window, payload + first, second);
            }
        }
        tcb->window_used_size += payload_size;
        tcb->window_tail = (tcb->window_tail + payload_size) % tcb->window_size;
        tcb->ack += payload_size;
        send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0);
```

Receive logic. Don't forget to update `ack` with `payload_size`, and tell the peer how much window space we have left in `send_tcp_pack`:

![image-20260311154401741](../assets/自制操作系统（26）：TCP（三）——接发数据/image-20260311154401741.png)

From Wireshark's capture, our updates look correct. But when a message arrives, the system crashes — adding this fix:

```cpp
    } else {
        SpinlockGuard guard(process_list_lock);
        remove_from_process_queue(poll_queue, current_pcb()->pid);
        for (int i = 0; i < fd_num; ++i) {
            if (current_pcb()->fd[fds[i].fd] != nullptr) {
                v_setpoll(current_pcb(), fds[i].fd, nullptr);
            }
        }
        return 1;
    }
```

It was a use-after-free bug. After fixing this, data still wasn't being received — probably a peek issue.

![image-20260311161038097](../assets/自制操作系统（26）：TCP（三）——接发数据/image-20260311161038097.png)

After fixing peek, we could receive messages, but display was still problematic...

First, receiving data caused infinite newlines (probably because my `read` return value checking wasn't strict enough);

Second, the console would block, meaning I had to send data before seeing the server's response.

I asked Claude, and it's likely that my console device's `peek` is too permissive — it should check for newline characters.

#### Modifying the Device File

The `read` function should put characters into `console_buffer` until the buffer is full, a newline is encountered, or the specified size is exceeded. Before that, `read` won't write anything to the provided buffer. Let's modify `console_peek` to eat input characters into a buffer, and provide a `line_ready` flag to `console_read` that won't be set to true until we press Enter:

```cpp
static int console_peek() {
    while (keyboard_haschar() && !line_ready) {
        char c = keyboard_getchar();

        if (c == '\b') {
            if (line_len > 0) {
                --line_len;
                terminal_write("\b", 1);   // echo backspace
            }
            continue;
        }

        if (c == '\n') {
            line_buf[line_len++] = '\n';
            terminal_write("\n", 1);   // echo newline
            line_ready = true;
            break;
        }

        if (c >= 32 && c <= 126 && line_len < sizeof(line_buf) - 1) {
            line_buf[line_len++] = c;
            terminal_write(&c, 1);     // echo visible characters
        }
    }
    return line_ready ? (int)line_len : 0;
}

static int console_read(char* buffer, uint32_t offset, uint32_t size) {
    while (!line_ready) {
        console_peek();
        if (!line_ready)
            asm volatile("pause");
    }

    uint32_t n = line_len < size ? line_len : size;
    memcpy(buffer, line_buf, n);

    // reset buffer
    line_len = 0;
    line_ready = false;
    return n;
}
```

Now with `peek`, as long as we don't press Enter, `read` won't be called. And when `read` is called, the line data is already ready, so it won't block.

![image-20260311170758686](../assets/自制操作系统（26）：TCP（三）——接发数据/image-20260311170758686.png)

And just like that, our TCP can receive data!

### Sending Data

We won't implement retransmission for now. After sending data, we have to wait for the ACK to come back before sending more. Actually, we could even skip waiting for ACK entirely.

#### Anti-Perfectionism

Perfectionism is the enemy of execution. The truth is, we can only accomplish things through trial and iteration, not by having "great ideas" from the start. So for sending data, I'm not going to make it elegant — as long as it works, that's enough for me.

So here's my implementation of data sending:

```cpp
int tcp_write(socket& sock, char* buffer, uint32_t size) {
    TCB* tcb = sock.data.tcp.block;
    int ret = send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, buffer, size);
    return ret;
}
```

What we wanted is really this simple, isn't it?

![image-20260311190742502](../assets/自制操作系统（26）：TCP（三）——接发数据/image-20260311190742502.png)

![image-20260311195452542](../assets/自制操作系统（26）：TCP（三）——接发数据/image-20260311195452542.png)

And just like that, we successfully implemented a TCP client and server, with simple send/receive capability, keeping sending as simple as possible.

#### POC: A Simple HTTP Client

We don't have DNS yet, so we have to manually enter IP addresses.

![image-20260311203952462](../assets/自制操作系统（26）：TCP（三）——接发数据/image-20260311203952462.png)

![image-20260311204020229](../assets/自制操作系统（26）：TCP（三）——接发数据/image-20260311204020229.png)

We now have a working TCP network stack!

---

In the next section, we'll look at closing connections.
