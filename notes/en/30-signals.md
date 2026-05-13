## Homemade OS (30): Signals

```
This article is not complete... under construction.
```

When a user-mode program gets stuck, our OS would have to reboot — unbearable! Let's implement signals right away!

A signal is essentially a flag. When a process returns from an interrupt carrying this flag, it enters the corresponding signal handler.

#### Signal Implementation with Only System Handlers

```cpp
    uint32_t signal;
```

Add one field to the PCB.

```
common_interrupt_handler:
    pusha
    # save segment registers
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    pushl %esp
    
    call inner_interrupt_handler
    addl $4, %esp
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popa
    addl $8, %esp
    iret
```

#### From Polling Model to Interrupt-Driven

Our current `getline` keeps polling the keyboard driver for characters — if none, it calls `pause`. We need to change this: move the buffer to the TTY side and use `terminal_input` to deliver characters.

```cpp
void keyboard_interrupt_handler(registers* /* regs */) {
    uint8_t scancode = hal_inb(0x60);
    if (scancode & KEY_RELEASED_MASK) {
        if ((scancode ^ KEY_RELEASED_MASK) == 0x2A || (scancode ^ KEY_RELEASED_MASK) == 0x36) {
            is_shift_pressed = false;
        }
        return;
    }
    if (scancode == 0x2A || scancode == 0x36) {
        is_shift_pressed = true;
        return;
    }
    terminal_input(scancode_to_ascii_table[scancode][is_shift_pressed]);
    return;
}
```

`terminal_input` dispatches different branches based on the input, such as sending an interrupt:

```cpp
void terminal_input(char c) {
    if (c == 0x03) {
        send_signal(foreground_pid, SIGNAL::SIGINT);
        terminal_write("^C\n", 3);
        return;
    }

    rb_write(c);

    {
        SpinlockGuard guard(process_list_lock);
        PCB* cur;
        while(cur = tty_wait_queue) {
            remove_from_process_queue(tty_wait_queue, cur->pid);
            cur->state = process_state::READY;
            insert_into_scheduling_queue(cur->pid);
        }
    }
}
```

Notice I've added two new things: `foreground_pid`, which indicates the PID of the foreground process currently running in our terminal (when a keyboard interrupt fires, any process may be scheduled, but there can only be one foreground process); and a wait queue, which we'll cover later.

```cpp
bool is_shift_pressed = false;
bool is_ctrl_pressed = false;

void keyboard_interrupt_handler(registers* /* regs */) {
    uint8_t scancode = hal_inb(0x60);
    if (scancode & KEY_RELEASED_MASK) {
        uint8_t released = scancode ^ KEY_RELEASED_MASK;
        if (released == 0x2A || released == 0x36)
            is_shift_pressed = false;
        if (released == 0x1D)
            is_ctrl_pressed = false;
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        is_shift_pressed = true;
        return;
    }
    if (scancode == 0x1D) {
        is_ctrl_pressed = true;
        return;
    }

    char c = scancode_to_ascii_table[scancode][is_shift_pressed];
    if (c && is_ctrl_pressed) {
        c = c & 0x1F;
    }
    if (c) {
        terminal_input(c);
    }
}
```

We add Ctrl key detection to the TTY driver (both left and right Ctrl have scancode `0x1D`). When Ctrl is pressed, we clear the upper 6 bits of the character to convert it into the corresponding control character.

#### foreground_pid

Setting the foreground PID allows interrupting the real foreground process.

#### Problem

Our signals only take effect when Enter is pressed, because the signal is blocked inside the input loop — we're stuck in kernel mode, and `do_signal` only runs when returning to user mode. We need to do two things: immediately put the process back into the ready queue after sending a signal, and check for pending signals in `terminal_read_char`, breaking out of the loop if one arrives.

#### More Problems

How do we simultaneously release processes waiting on a pipe?

---

In the next chapter, we'll implement ProcFS and use it along with signals to build `ps` and `kill`.
