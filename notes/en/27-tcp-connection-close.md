## Homemade OS (27): TCP (Part 4) — Connection Close

Today we implement connection teardown.

Connection close is the so-called "four-way wave," which in simple terms goes like this:

A: I'm done sending (FIN). B: OK (ACK). (A moment later) I'm done too (or "OK, I'm done too", FINACK)

A: OK. Then both sides enter a `TIME_WAIT` state, wait for some time, and finally destroy the TCB.

### Active Close

The active closer sends FINACK, transitions its state to `FIN_WAIT1` — this indicates we've finished sending data and are waiting for confirmation that the other side has acknowledged this. Once we receive an ACK from the peer, we transition to `FIN_WAIT2`, indicating the peer knows we won't send more data, and we're waiting for them to finish sending their data and send a FIN:

```cpp
int tcp_close(socket& sock) {
    SpinlockGuard guard(sock.lock);
    TCB* tcb = sock.data.tcp.block;
    PCB* cur;
    // There's not much I can do with the wait_queue in the socket here,
    // because if a process is blocked on it, it can't call close() proactively
    // unless some external event occurs — in which case the whole process would be destroyed anyway
    sock.valid = 0;

    send_tcp_pack(tcb, ((uint8_t)tcp_flags::FIN | (uint8_t)tcp_flags::ACK), nullptr, 0);
    tcb->state = tcb_state::FIN_WAIT1;
}
```

FIN requires a **virtual byte** just like SYN, because it increments the sequence number, ensuring reliable delivery of our termination signal (without this, we couldn't distinguish ACKs for transmitted data from ordinary ACKs):

```cpp
// inside send_tcp_pack
// Flags that need reliable acknowledgment all require virtual bytes
    if (((uint8_t)flags & (uint8_t)tcp_flags::SYN) || ((uint8_t)flags & (uint8_t)tcp_flags::FIN)) {
        tcb->seq += 1; // virtual byte
    }
```

Our handling for the `FIN_WAIT1` state:

```cpp
    case tcb_state::FIN_WAIT1:
        if ((header->flags & (uint8_t)tcp_flags::ACK) != 0) {
            // Acknowledged that we've finished sending
            if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) { // If the peer sent FIN along with the ACK
                tcb->ack = ntohl(header->seq_num) + 1; // FIN occupies a virtual byte
                send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0);
                tcb->state = tcb_state::TIME_WAIT;
                time_wait(tcb);
            } else {
                // If only ACK was sent, just transition to FIN_WAIT2
                tcb->state = tcb_state::FIN_WAIT2;
            }
        } else if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) { // Received FIN without ACK — peer is also closing
            tcb->state = tcb_state::CLOSING; // Transition to CLOSING
            tcb->ack = ntohl(header->seq_num) + 1; // FIN occupies a virtual byte
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0);
        }
        break;
```

Note the virtual byte handling: when we receive a FIN flag, we must increment the ACK by one virtual byte.

There's also an interesting scenario: both sides send FIN simultaneously... This leads to the peculiar `CLOSING` state, which we'll discuss at the end.

Now let's look at the new state `FIN_WAIT2`. In this state, the peer knows we won't send any more data, but they may still have data to send:

```cpp
    case tcb_state::FIN_WAIT2:
        if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) { // Peer is done
            tcb->ack = ntohl(header->seq_num) + 1;
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0); // OK
            tcb->state = tcb_state::TIME_WAIT;
            time_wait(tcb);
        }
        // If not a FIN, the peer might still have data to send — let execution
        // fall through to ESTABLISHED handling, but keep the state unchanged
        [[fallthrough]];
    case tcb_state::ESTABLISHED:
    {
```

So we check whether the peer includes a FIN flag; if not, we proceed with ACK processing.

Do you see a problem with the flow above? Think about it.

The issue is that both `FIN_WAIT1` and `FIN_WAIT2` mean we won't send any more data. But the peer might still have data to send, and our `FIN_WAIT2` code doesn't handle the case where they send data WITH a FIN flag (which is entirely possible). So let's revise the `FIN_WAIT2` code:

```cpp
    case tcb_state::FIN_WAIT2:
        [[fallthrough]];
    case tcb_state::ESTABLISHED:
    {
        if (ntohl(header->seq_num) != tcb->ack) { // Simple implementation: drop out-of-order packets
            break;
        }
        if ((header->flags & (uint8_t)tcp_flags::RST) == 0) {
            // todo: properly handle RST
            break;
        }
        if ((header->flags & (uint8_t)tcp_flags::ACK) == 0) {
            break;
        }
        
        ...process the data packet — since we're here, let's handle data first

        if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) {
            tcb->ack = ntohl(header->seq_num) + 1;
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0); // OK
            if (tcb->state == tcb_state::FIN_WAIT2) { // If we're actually FIN_WAIT2
                tcb->state = tcb_state::TIME_WAIT;
                time_wait(tcb);
                break;
            } else {
                ...passive close flow
            }
        }
        
```

And `FIN_WAIT1` needs to be revised accordingly too. Our original `FIN_WAIT1` flow also needs to handle data first. Let's refactor `FIN_WAIT1` — notice the code below is logically equivalent to the original:

```cpp
    case tcb_state::FIN_WAIT1:
        if ((header->flags & (uint8_t)tcp_flags::ACK) != 0) {
            // Acknowledged that we've finished sending
            tcb->state = tcb_state::FIN_WAIT2;
            if ((header->flags & (uint8_t)tcp_flags::FIN) == 0) { // If the peer sent FIN along with the ACK
                tcb->ack = ntohl(header->seq_num) + 1; // FIN occupies a virtual byte
                send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0);
                tcb->state = tcb_state::TIME_WAIT;
                time_wait(tcb);
            }
        } else if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) { // Received FIN without ACK — peer is also closing
            tcb->state = tcb_state::CLOSING; // Transition to CLOSING
            tcb->ack = ntohl(header->seq_num) + 1; // FIN occupies a virtual byte
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0);
        }
        break;
```

That is, for the first case where `FIN_WAIT1` receives an ACK, we can just transition to `FIN_WAIT2`! (Think about it: the `FIN_WAIT2` handling of FINACK is the same as the `FIN_WAIT1` handling flow!)

```cpp
    case tcb_state::FIN_WAIT1:
        if ((header->flags & (uint8_t)tcp_flags::ACK) != 0) {
            tcb->state = tcb_state::FIN_WAIT2; // Acknowledged that we've finished sending
        } else if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) { // Received FIN without ACK — peer is also closing
            tcb->state = tcb_state::CLOSING; // Transition to CLOSING
            tcb->ack = ntohl(header->seq_num) + 1; // FIN occupies a virtual byte
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0);
        }
        [[fallthrough]];
    case tcb_state::FIN_WAIT2:
        [[fallthrough]];
    case tcb_state::ESTABLISHED:
    {
        if (ntohl(header->seq_num) != tcb->ack) { // Simple implementation: drop out-of-order packets
            break;
        }
        if ((header->flags & (uint8_t)tcp_flags::RST) == 0) {
            // todo: properly handle RST
            break;
        }
        if ((header->flags & (uint8_t)tcp_flags::ACK) == 0) {
            break;
        }
        
        ...process the data packet — since we're here, let's handle data first

        if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) {
            tcb->ack = ntohl(header->seq_num) + 1;
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0); // OK
            if (tcb->state == tcb_state::FIN_WAIT2) { // If we're actually FIN_WAIT2
                tcb->state = tcb_state::TIME_WAIT;
                time_wait(tcb);
                break;
            } else {
                ...passive close flow
            }
        }
```

Wait — there's one more case we haven't handled: what if I send FIN and simultaneously the peer also sends FIN WITH data? How troublesome. But in this case, if the peer sends FIN without ACK and includes data while we're in `FIN_WAIT1`, that's quite rude. In this scenario, their data will just be dropped.

#### TIME_WAIT State and `time_wait()`

The `TIME_WAIT` state is entered when both sides have agreed that we (the active closer) will stop sending data, and the peer has signaled they're also done, and we've sent our "OK" acknowledgment. When entering this state, we call `time_wait()` to start a TCB destruction timer, beginning the countdown of the TCB's lifecycle. This state prevents our "I acknowledge you're done sending" packet from being lost. With this state, if the peer retransmits their FIN, we can still reply. Without it, if we destroy the connection immediately, the peer's FIN would go unanswered indefinitely — a dilemma. Since the peer may have attached data to that last FIN, they'd be anxiously wondering "Did the other side receive my final data?" The `TIME_WAIT` state provides a safety net for reliably receiving that last data.

If the timer fires and our ACK can't even be sent, that means the connection itself is fundamentally broken. In that case, there's nothing we can do about missing the peer's last data — if the peer never gets a reply, they'll have to give up too.

Our bomb disposal kit:

```cpp
static void destroy_tcb(pid_t pid, void* tcb) {
    kfree(reinterpret_cast<TCB*>(tcb)->window);
    kfree(tcb);
}

static void time_wait(TCB* tcb) {
    register_timer(pit_get_ticks() + 300, &destroy_tcb, nullptr); // 10ms = 1 tick, so 300 ticks = 3 seconds
}
```

State handler:

```cpp
    case tcb_state::TIME_WAIT:
        if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) {
            // No need to add ACK again; we definitely already did
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0);

        }
        break;
```

Here, if we receive a FIN, the specification says we should reset the timer. Not resetting it effectively means the timer limits the "attempt period for sending the last ACK." You could also reset it so each retry is independently timed; I've chosen the simple approach.

#### Debugging

Let's test as the active connection closer:

![image-20260312120535751](../assets/自制操作系统（27）：TCP（四）——关闭连接/image-20260312120535751.png)

Test passed!

### Passive Close

Passive close is the process where the peer sends us a FIN and we handle it. When we receive the peer's FIN, we simply ACK and transition to `CLOSE_WAIT`. We can still send data freely from our side.

```cpp
        if ((header->flags & (uint8_t)tcp_flags::FIN) != 0) { // Peer notifies it won't send more data
            tcb->ack = ntohl(header->seq_num) + 1;
            send_tcp_pack(tcb, (uint8_t)tcp_flags::ACK, nullptr, 0); // OK
            if (tcb->state == tcb_state::FIN_WAIT2) { // If we're actually FIN_WAIT2
                tcb->state = tcb_state::TIME_WAIT;
                time_wait(tcb);
            } else { // Passive close scenario
                tcb->state = tcb_state::CLOSE_WAIT;
                wake_all_queue();
            }
        }
```

The only thing to note is that we need to wake up any processes still waiting on the queue — there's no more data to receive. We call the encapsulated `wake_all_queue`:

```cpp
void wake_all_queue(socket* sock){
    { // blocking read
        SpinlockGuard guard(process_list_lock);
        PCB* cur;
        while(cur = sock->wait_queue) {
            remove_from_process_queue(sock->wait_queue, cur->pid);
            cur->state = process_state::READY;
            insert_into_scheduling_queue(cur->pid);
        }
    }
    { // poll
        SpinlockGuard guard(process_list_lock);
        PCB* cur;
        while((sock->poll_queue != nullptr) && (*(sock->poll_queue) != nullptr) &&
            (cur = *(sock->poll_queue))) {
            remove_from_process_queue(*(sock->poll_queue), cur->pid);
            cur->state = process_state::READY;
            insert_into_scheduling_queue(cur->pid);
        }
    }
}
```

Then in `tcp_close`, we transition to different states based on our current state:

```cpp
int tcp_close(socket& sock) {
    ...
    send_tcp_pack(tcb, ((uint8_t)tcp_flags::FIN | (uint8_t)tcp_flags::ACK), nullptr, 0);
    if (tcb->state == tcb_state::ESTABLISHED) {
        tcb->state = tcb_state::FIN_WAIT1;
    } else if (tcb->state == tcb_state::CLOSE_WAIT) {
        tcb->state = tcb_state::LAST_ACK;
    } else {
        printf("warning: unexpected state %d\n", tcb->state);
    }
    return 0;
}
```

The `LAST_ACK` state is straightforward — we just wait for one final ACK:

```cpp
    case tcb_state::LAST_ACK:
        if ((header->flags & (uint8_t)tcp_flags::ACK) != 0) { // Received the final ACK
            destroy_tcb(cur_process_id, tcb);
        }
        break;
```

Passive close is really that simple and straightforward.

```cpp
case tcb_state::CLOSING:
        if ((header->flags & (uint8_t)tcp_flags::ACK) != 0) {
            // Our previous FIN's ACK was received, so the peer knows we won't send data
            // But did the peer receive the ACK we sent earlier? If not, they'll resend FIN
            // So we need to enter TIME_WAIT
            tcb->state = tcb_state::TIME_WAIT;
            time_wait(tcb);
        }
        break;
```

Might as well implement `CLOSING` while we're at it.

#### Debugging

![image-20260312123929042](../assets/自制操作系统（27）：TCP（四）——关闭连接/image-20260312123929042.png)

Passive close is correctly implemented.

The connection is indeed closed, but... are we safe?

### Resource Management

Our TCB can exist in various places throughout the system (Socket, queue, map, TCP Handler, Timer)... Yet our deallocation is quite haphazard — we might forget to free, double-free, or worst of all, use after free. One slip and the system is in danger!

At the very least, let's eliminate the last two possibilities. So let's add **reference counting** to TCB — or rather, let's use `shared_ptr` for TCB!

But our library doesn't have `shared_ptr` yet. Let's write our own.

#### shared_ptr

The logic of `shared_ptr` is simple: it holds a reference count inside. When we copy it, the count increments; when we destroy it, the count decrements. When the count reaches zero, the pointed-to object's destructor is called.

```cpp
int refcount;

void grab() {
    __atomic_fetch_add(&refcount, 1, __ATOMIC_RELAXED);
}

bool put() {
    return __atomic_sub_fetch(&refcount, 1, __ATOMIC_ACQ_REL) == 0;
}
```

We use atomic operations for incrementing and decrementing the counter.

I don't write much modern C++, so I gave it my best shot — basically wrote a version and had Claude guide me through revisions:

![image-20260312172746098](../assets/自制操作系统（27）：TCP（四）——关闭连接/image-20260312172746098.png)

And just like that, we've added smart pointer protection. Pretty nice!

---

Our TCP stack is now quite practical! In the next chapter, I'll share some exciting things you can do with your current TCP stack.
