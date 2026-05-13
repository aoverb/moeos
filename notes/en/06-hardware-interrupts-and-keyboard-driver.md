## Homemade OS (6): Hardware Interrupts and Keyboard Driver

In the previous article, we set up the GDT and IDT, giving our system the ability to handle software interrupts. Today, we'll enable our OS to handle hardware interrupts.

### Hardware Interrupts

Hardware interrupts, as the name implies, are interrupts sent to the CPU by hardware devices. For example, keyboard input, disk reads, etc., all send interrupts to the CPU to inform it of the device's current state and any new events.

However, hardware doesn't directly send interrupts to the CPU. The bridge between them is a chip called the **8259 PIC (Programmable Interrupt Controller)**.

#### Hardware Interrupt Remapping

Due to some early incompatible design decisions, the 8259 has a default mapping from IRQ to IDT as follows:

**Master PIC**: Handles IRQ 0-7. In the default IBM PC configuration, these are mapped to IDT **0x08 - 0x0F**.

**Slave PIC**: Handles IRQ 8-15. These are mapped to IDT **0x70 - 0x77**.

![image-20260130115718152](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%886%EF%BC%89%EF%BC%9A%E7%A1%AC%E4%BB%B6%E4%B8%AD%E6%96%AD%E4%B8%8E%E9%94%AE%E7%9B%98%E9%A9%B1%E5%8A%A8/image-20260130115718152.png)

But in the previous section, we learned that software interrupts correspond to IDT **0x00 - 0x1F** — the software and hardware interrupts overlap! So we need to reprogram these hardware interrupts and remap them.

#### Talking to Hardware: I/O Ports

To send commands to the PIC, we must use the `in` and `out` assembly instructions to operate **I/O ports**. To make this convenient for C code, we wrap them in `io.h`:

```c
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}
```

#### PIC Initialization

![image-20260130130528764](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%886%EF%BC%89%EF%BC%9A%E7%A1%AC%E4%BB%B6%E4%B8%AD%E6%96%AD%E4%B8%8E%E9%94%AE%E7%9B%98%E9%A9%B1%E5%8A%A8/image-20260130130528764.png)

This part is just copy-pasted code, nothing much to say.

#### Setting Up Interrupt Handler 33

The macros we defined in the previous article come in handy here. Interrupt 33 has no error code, so we can just use the macro to generate it.

![image-20260130135104684](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%886%EF%BC%89%EF%BC%9A%E7%A1%AC%E4%BB%B6%E4%B8%AD%E6%96%AD%E4%B8%8E%E9%94%AE%E7%9B%98%E9%A9%B1%E5%8A%A8/image-20260130135104684.png)

We read the scancode provided by the Keyboard Encoder through I/O port 0x60:

![image-20260130164734921](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%886%EF%BC%89%EF%BC%9A%E7%A1%AC%E4%BB%B6%E4%B8%AD%E6%96%AD%E4%B8%8E%E9%94%AE%E7%9B%98%E9%A9%B1%E5%8A%A8/image-20260130164734921.png)

Notice in the code above, after reading the port, we also need to send a `0x20` (EOI, End of Interrupt) command to the PIC port. Otherwise, the PIC will think the CPU is still busy and won't send the next interrupt.

Let's add this to our unified handler and see if we can trigger a keyboard interrupt:

![image-20260130135144762](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%886%EF%BC%89%EF%BC%9A%E7%A1%AC%E4%BB%B6%E4%B8%AD%E6%96%AD%E4%B8%8E%E9%94%AE%E7%9B%98%E9%A9%B1%E5%8A%A8/image-20260130135144762.png)

We can see our scancode now!

### Keyboard Driver

Since we can receive scancodes from the keyboard, the next step is to implement our keyboard driver.

A good keyboard driver should hide as many hardware details as possible (through interfaces).

So let's create `kernel/driver/keyboard.h` and define the following functions:

```cpp
#ifndef _DRIVER_KEYBOARD_H
#define _DRIVER_KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

void keyboard_init();
char keyboard_getchar();
bool keyboard_haschar();

#ifdef __cplusplus
}
#endif

#endif
```

Then we'll implement them one by one.

#### Before Initialization: Hardware Interrupt Registration

Let's think about what keyboard initialization needs to do. First, it has to route keyboard input through our keyboard interrupt handler. But our current interrupt handler is embedded in the IDT. To replace it with our handler from `keyboard.c`, we need to write a hardware interrupt handler registration mechanism.

Actually, keeping the interrupt handling logic inside the IDT is somewhat coupled. We need to separate these two parts before we can register handlers.

##### Before Registration: Separate IDT from ISR (Interrupt Service Routines)

![image-20260130190451238](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%886%EF%BC%89%EF%BC%9A%E7%A1%AC%E4%BB%B6%E4%B8%AD%E6%96%AD%E4%B8%8E%E9%94%AE%E7%9B%98%E9%A9%B1%E5%8A%A8/image-20260130190451238.png)

Our ISR currently only needs a registration function and a variable shared with functions that need to call interrupts.

(We'll wrap all of this into classes later...)

![image-20260130190618491](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%886%EF%BC%89%EF%BC%9A%E7%A1%AC%E4%BB%B6%E4%B8%AD%E6%96%AD%E4%B8%8E%E9%94%AE%E7%9B%98%E9%A9%B1%E5%8A%A8/image-20260130190618491.png)

Then we can determine which interrupts are hardware interrupts by range and forward the calls to the corresponding registered functions.

```cpp
void keyboard_init() {
    register_interrupt_handler(33, keyboard_interrupt_handler);
    while (hal_inb(0x64) & 0x01) {
        hal_inb(0x60); // Read and discard data
    }
}
```

Looking back at our `keyboard_init`, we can directly register our defined handler function and then clear the keyboard buffer.

#### Keyboard Interrupt Handling Logic

```cpp
void keyboard_interrupt_handler(registers* /* regs */) {
    uint8_t scancode = hal_inb(0x60);
    printf("%d ", scancode);
    return;
}
```

The current handler logic outputs directly to screen, just to confirm our refactoring is correct. But what we really need is to set up an internal buffer and write the received characters into it.

##### Ring Buffer

![image-20260130194204377](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%886%EF%BC%89%EF%BC%9A%E7%A1%AC%E4%BB%B6%E4%B8%AD%E6%96%AD%E4%B8%8E%E9%94%AE%E7%9B%98%E9%A9%B1%E5%8A%A8/image-20260130194204377.png)

When the buffer is full, it overwrites the oldest data.

We can use this buffer to write our data.

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
    rb_write(&kbd_buffer, scancode_to_ascii_table[scancode][is_shift_pressed]);
    return;
}
```

As shown above, we first convert the scancode to the corresponding ASCII character, then write it into the buffer.

I also added a check for whether Shift is held down to determine whether to write uppercase characters to the buffer.

```cpp
void keyboard_flush() {
    rb_flush();
}

char keyboard_getchar() {
    return rb_read(&kbd_buffer);
}

bool keyboard_haschar() {
    return kbd_buffer.head != kbd_buffer.tail;
}
```

The rest of the implementation is straightforward. Our keyboard driver has reached a usable state.

### Further Enhancing the libc

With the keyboard driver, we can now enhance our libc.

#### getline

Implement a `getline()` function in the stdio library for line-by-line keyboard input:

```cpp
void getline(char* buf, uint32_t size) {
    keyboard_flush();
    uint32_t i = 0;

    while (i < size - 1) {
        while (!keyboard_haschar()) {
            asm volatile("pause"); 
        }

        char c = keyboard_getchar();

        if (c == '\n') {
            buf[i] = '\0';
            printf("\n");
            return;
        }

        if (c >= 32 && c <= 126) {
            buf[i++] = c;
            printf("%c", c);
        }
    }

    buf[i] = '\0';
}
```

Then we can use this in `kernel_main` to implement a toy shell:

```cpp
extern "C" void kernel_main(multiboot_info_t* mbi) {
    terminal_initialize(mbi);
    print_rumia();

    printf("HAL initializing...");
    hal_init();
    keyboard_init();
    asm volatile ("sti");
    printf("OK\n");

    print_info();

    char input[64];
    while (1) {
        printf("LoliOS>");
        getline(input, 64);
        printf("\n");
    }
}
```

![image-20260130202516614](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%886%EF%BC%89%EF%BC%9A%E7%A1%AC%E4%BB%B6%E4%B8%AD%E6%96%AD%E4%B8%8E%E9%94%AE%E7%9B%98%E9%A9%B1%E5%8A%A8/image-20260130202516614.png)

Looks pretty good, right? But our input doesn't support backspace yet. Let's add that feature.

#### Backspace

We didn't write backspace clearing logic in `terminal_write`, so we need to add it. Specifically, we move back one position and fill the current cell with a black rectangle.

```cpp
if (data[i] == '\b') {
    if (terminal_col == 0 && terminal_row == 0) {
        return;
    }
    if (terminal_col == 0) {
        --terminal_row;
        terminal_col = terminal_cols;
    }
    --terminal_col;
    terminal_fill_rect(terminal_col * FONT_WIDTH, 
                      terminal_row * FONT_HEIGHT, 
                      FONT_WIDTH, 
                      FONT_HEIGHT, 
                      0x00000000);
    continue;
}
```

### Toy Shell Demo

I implemented `strcmp`, so now I can use the following code for a simple shell:

```cpp
char input[256];
while (1) {
    print_lolios();
    printf(">");
    getline(input, 256);
    if (strcmp(input, "help") == 0) {
        printf("Hello user!");
        printf("This is ");
        print_lolios();
        printf("!\n");
        printf("The host here is ");
        print_rumia_text();
        printf("! Feel free!\n");
    } else if (strcmp(input, "rumia") == 0) {
        print_rumia();
    } else {
        print_rumia_text();
        printf(": Unknown command '%s'!\n", input);
    }
    printf("\n");
}
```

![image-20260130210240248](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%886%EF%BC%89%EF%BC%9A%E7%A1%AC%E4%BB%B6%E4%B8%AD%E6%96%AD%E4%B8%8E%E9%94%AE%E7%9B%98%E9%A9%B1%E5%8A%A8/image-20260130210240248.png)

Feels great!

### Aside: An Interesting Discovery

Earlier, I thought: since hardware interrupts are also interrupts, and if no interrupt handler is set, the system would definitely reboot. So since I had set up the IDT and GDT but only configured software interrupt handlers, pressing a key after entering the system should cause a reboot. But to my surprise, it didn't happen. I asked AI and was told I hadn't initialized the PIC or unmasked interrupts. But even after doing all that, nothing happened.

Then it suddenly occurred to me: my `kernel_main` returns immediately after initialization. Could it be that my OS had already exited? So I added an infinite loop at the end, and sure enough, something happened — my interrupt handler reported a #13 (General Protection Fault) interrupt, which kept printing in a loop. The GPF occurred because the interrupt handler couldn't find a handler for IRQ 33 (keyboard) in the IDT. **The `iret` returns to the instruction that triggered the exception, and that instruction keeps triggering the same exception.**

I thought the OS was still running because the display was normal, but I had fallen into user-space thinking while writing an OS.

---

Today, we learned how to set up hardware interrupts, remap hardware interrupts, read/write I/O ports, refactor IDT and ISR, write hardware interrupt registration logic for future hardware interrupts, write a simple keyboard driver, further enhance the libc, and build a simple shell based on all this work... Truly not easy!

I originally planned to implement the timer interrupt and `sleep()` function today as well, but we've already done enough. That will be for the next article.

References:

https://www.brokenthorn.com/Resources/OSDev19.html - **Operating Systems Development - Keyboard**. A great article explaining the ins and outs of keyboard handling.
