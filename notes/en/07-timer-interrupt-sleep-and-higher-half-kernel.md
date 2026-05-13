## Homemade OS (7): Timer Interrupt, sleep(), and Migrating the Kernel to the Higher Half

In the previous section, we successfully took over keyboard interrupts, refactored some code, implemented a keyboard driver, and built a toy shell. Today we're going to tackle something more hardcore, but first, let's start with something light — implementing a `sleep()` function.

### PIT (Programmable Interval Timer)

The PIT receives an input signal from an oscillator running at a fixed frequency (1,193,182 Hz), and based on the configuration programmed through gate inputs, produces different gate outputs on different channels. Like the keyboard, the PIT is hardware that communicates with the CPU via I/O ports. Therefore, we can program it through I/O ports.

The PIT has three logical channels. Channel 0 is connected to the PIC chip, so it can generate an IRQ 0 interrupt signal — which is the timer interrupt we'll configure today. Channel 1 was used in early computers to refresh the charge stored in DRAM but is now obsolete. Channel 2 is connected to the speaker and can be configured to produce beep sounds at different frequencies.

Each channel can be configured with different operating modes. We don't need to understand every mode; let's focus on the one we'll use today: Mode 2, Rate Generator.

#### Rate Generator

The Rate Generator produces periodic pulses. When switched to this mode, the internal counter loads a preset reload value on the next falling edge of the input signal and sets the channel output to high level. The counter decrements on each falling edge of the oscillator input. When the count reaches 1, the channel output goes low. When it reaches 0, the output goes back high, and the counter is reloaded with the reload value. This repeats, producing extremely narrow negative pulses at the output. IRQ pins are rising-edge triggered by default, so an IRQ 0 interrupt is triggered when the count reaches 0 (going from low to high).

#### I/O Ports

```
I/O port     Usage
0x40         Channel 0 data port (read/write)
0x41         Channel 1 data port (read/write)
0x42         Channel 2 data port (read/write)
0x43         Mode/Command register (write only, a read is ignored)
```

(Reference: OSDev Wiki)

The Mode/Command register configuration rules are:

```
Bits         Usage
7 and 6      Select channel :
                 0 0 = Channel 0
                 0 1 = Channel 1
                 1 0 = Channel 2
                 1 1 = Read-back command (8254 only)
5 and 4      Access mode :
                 0 0 = Latch count value command
                 0 1 = Access mode: lobyte only
                 1 0 = Access mode: hibyte only
                 1 1 = Access mode: lobyte/hibyte
3 to 1       Operating mode :
                 0 0 0 = Mode 0 (interrupt on terminal count)
                 0 0 1 = Mode 1 (hardware re-triggerable one-shot)
                 0 1 0 = Mode 2 (rate generator)
                 0 1 1 = Mode 3 (square wave generator)
                 1 0 0 = Mode 4 (software triggered strobe)
                 1 0 1 = Mode 5 (hardware triggered strobe)
                 1 1 0 = Mode 2 (rate generator, same as 010b)
                 1 1 1 = Mode 3 (square wave generator, same as 011b)
0            BCD/Binary mode: 0 = 16-bit binary, 1 = four-digit BCD
```

For example, to enable channel 0 in mode 2, the operation would be `out 0x43 0b00110100`. Note that I set Access mode to 11, meaning when setting the reload count, I'll first write the low byte to the channel, then the high byte.

The current frequency is 1,193,182 Hz. If we set the reload count to 11,932, we get an interrupt approximately every 10ms. The corresponding operations are `out 0x40 0x9c` and `out 0x40 0x2e`.

#### sleep() Implementation

Let's start writing the code right away.

```cpp
static volatile uint32_t ticks;

void timer_interrupt_handler(registers* /* regs */) {
    ++ticks;
    hal_outb(0x20, 0x20); // EOI
    return;
}

void pit_init() {
    asm volatile ("cli");
    ticks = 0;
    register_interrupt_handler(32, timer_interrupt_handler);
    hal_outb(0x43, 0x34);
    hal_outb(0x40, 0x9c);
    hal_outb(0x40, 0x2e);
}

void pit_sleep(uint32_t ms) {
    uint32_t saved_ticks = ticks;
    uint32_t ceilling_10ms = (ms + 9) / 10;
    while (ticks - saved_ticks < ceilling_10ms) {
        asm volatile ("hlt");
    }
}
```

Despite all the explanation above, the code is relatively simple.

With this, we've implemented a timer with 10ms precision. Let's use it in `kernel_main`:

```cpp
keyboard_init();
pit_init();
asm volatile ("sti");

...

char input[256];
while (1) {
    ...
    } else if (strcmp(input, "tick") == 0) {
        uint8_t ticks = 0;
        while (++ticks) {
            printf("%d\n", ticks);
            pit_sleep(1000);
        }
    }
    ...
}
```

I started a test eagerly, but sleep didn't work. After some investigation, I found that I had masked all IRQs except the keyboard during PIC initialization.

I decided that IRQ masking/unmasking shouldn't be done uniformly here but should be left to the hardware drivers. So I wrapped the masking/unmasking logic in `pic.h`:

```cpp
void pic_enable_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq -= 8;
    }
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

void pic_disable_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq -= 8;
    }

    value = inb(port) | (1 << irq);
    outb(port, value);
}
```

Then wrapped in `hal.h`:

```cpp
void hal_enable_irq(uint8_t irq) {
    asm volatile ("cli");
    pic_enable_irq(irq);
}
void hal_disable_irq(uint8_t irq) {
    asm volatile ("cli");
    pic_disable_irq(irq);
}
```

Now I can call enable/disable logic uniformly within my drivers:

```cpp
void pit_init() {
    asm volatile ("cli");
    ticks = 0;
    register_interrupt_handler(32, timer_interrupt_handler);
    hal_outb(0x43, 0x34);
    hal_outb(0x40, 0x9c);
    hal_outb(0x40, 0x2e);

    hal_enable_irq(0);
}
```

![image-20260131123742616](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%887%EF%BC%89%EF%BC%9A%E5%AE%9A%E6%97%B6%E4%B8%AD%E6%96%AD%E4%B8%8Esleep()%EF%BC%8C%E8%BF%81%E7%A7%BB%E5%86%85%E6%A0%B8%E5%88%B0%E9%AB%98%E5%8D%8A%E5%8C%BA/image-20260131123742616.png)

And our `sleep()` function now works correctly. However, I noticed some timing inaccuracy. After investigation, it's most likely a QEMU issue. We'll verify on real hardware later.

### Better to Suffer Now Than Later: Higher-Half Kernel

After chugging along this far, I realized I had missed something: the Higher-Half Kernel. Generally speaking, a Higher-Half Kernel means loading the kernel at around physical address 1MB but mapping it to around virtual address 3GB. This virtual mapping relies on the paging mechanism. We haven't enabled paging at all from the start. Now, for the sake of short-term pain (and long-term gain), along with AI-translated benefits from OSDev Wiki, let's refactor this part:

```
Advantages of a Higher-Half Kernel:
- Easier to set up VM86 processes: Since memory below 1MB belongs to user space, configuring Virtual 8086 Mode (VM86) becomes simpler.
- Better ABI compatibility: More generally, user-level applications no longer depend on how much memory the kernel occupies. Whether the kernel is at 0xC0000000, 0x80000000, or 0xE0000000, your applications can be uniformly linked at 0x400000, making the ABI more standardized.
- Supports full 32-bit addressing: If your OS is 64-bit, 32-bit applications will be able to use the full 32-bit address space.
- Memorable invalid pointers: You can use hex words like 0xCAFEBABE, 0xDEADBEEF, 0xDEADC0DE as invalid pointer markers.
```

#### Modifying linker.ld

During linking, we can specify where data should be loaded:

```ld
/* linker.ld */
ENTRY(_start)

SECTIONS
{
	/* Kernel starts at 1MB */
	. = 1M;

	/* Multiboot header must come first */
	.text BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot)
		*(.text)
	}

	/* Read-only data */
	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata)
	}

	/* Initialized data */
	.data BLOCK(4K) : ALIGN(4K)
	{
		*(.data)
	}

	/* Uninitialized data and stack (BSS) */
	.bss BLOCK(4K) : ALIGN(4K)
	{
		*(COMMON)
		*(.bss)
	}
}
```

Since we haven't enabled paging yet, our program is loaded at linear address 1MB. Remember flat mode? In flat mode, the linear address is the physical address, so we're actually loaded at 1MB.

To load into the higher half, we'll adopt a 3GB user program + 1GB kernel program split.

After boot, we want to achieve this environment:

1. Our kernel code doesn't know how much physical memory actually exists and thinks it's loaded at 3GB — we're inside the illusion of virtual address space created by paging.
2. In reality, the kernel is loaded at physical address 1MB.
3. All subsequent kernel instructions and memory allocations see virtual addresses; the kernel doesn't care where the physical addresses actually are.

```ld
ENTRY(_start)

SECTIONS
{
    /* Kernel will be loaded at 1MB */
    . = 0xC0100000;

    .text ALIGN(4K) : AT(ADDR(.text) - 0xC0000000) /* Note this... */
    {
        *(.multiboot)
        *(.text)
    }

	...
    
    _kernel_end = . - 0xC0000000;
}
```

Notice the "Note this" comment — we used an `AT` directive whose parameter evaluates to around 1MB. The `AT` directive tells where I actually want this content to be loaded. The `. = 0xC0100000;` above it sets where the code thinks it's loaded for execution purposes.

Now let's think about what happens when we link this way. GRUB, following our instructions, loads the sections around physical address 1MB and jumps to the entry point:

![image-20260131184119304](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%887%EF%BC%89%EF%BC%9A%E5%AE%9A%E6%97%B6%E4%B8%AD%E6%96%AD%E4%B8%8Esleep()%EF%BC%8C%E8%BF%81%E7%A7%BB%E5%86%85%E6%A0%B8%E5%88%B0%E9%AB%98%E5%8D%8A%E5%8C%BA/image-20260131184119304.png)

But our entry point is here — disaster!

So we need to do something: don't move the entry point here yet. Instead, set up a new entry point, configure the paging mechanism (specifically, map a few MB starting at the 3GB address to physical address 1MB), and then jump to `kernel_main`. Let's call this new entry point the **scaffold**.

#### Building the Scaffold

First, let's see how to set up page tables... but wait, what is a page table? What is paging?

##### Paging Mechanism

Remember the segmentation mechanism we introduced earlier? Segmentation gives us base + offset. If paging is not enabled, this calculation yields a physical address. If paging is enabled, this address is only a virtual address and needs another conversion to become a physical address. This conversion relies on the page tables in the paging mechanism. I won't go into detail about paging here — refer to the classic OSTEP book for the relevant chapters.

##### Two-Level Page Tables

We'll use a two-level page table structure: 1 page directory + 1 page table. The virtual address split is:

10 bits (PDE) + 10 bits (PTE) + 12 bits (offset)

![image-20260131193039247](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%887%EF%BC%89%EF%BC%9A%E5%AE%9A%E6%97%B6%E4%B8%AD%E6%96%AD%E4%B8%8Esleep()%EF%BC%8C%E8%BF%81%E7%A7%BB%E5%86%85%E6%A0%B8%E5%88%B0%E9%AB%98%E5%8D%8A%E5%8C%BA/image-20260131193039247.png)

In other words, take the PDE to find the page directory entry, then take the PTE to find the page table entry, and finally access the data/instruction at the corresponding page with the offset.

The initial page directory is set in the CR3 register.

![image-20260131193223495](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%887%EF%BC%89%EF%BC%9A%E5%AE%9A%E6%97%B6%E4%B8%AD%E6%96%AD%E4%B8%8Esleep()%EF%BC%8C%E8%BF%81%E7%A7%BB%E5%86%85%E6%A0%B8%E5%88%B0%E9%AB%98%E5%8D%8A%E5%8C%BA/image-20260131193223495.png)

![image-20260131193228854](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%887%EF%BC%89%EF%BC%9A%E5%AE%9A%E6%97%B6%E4%B8%AD%E6%96%AD%E4%B8%8Esleep()%EF%BC%8C%E8%BF%81%E7%A7%BB%E5%86%85%E6%A0%B8%E5%88%B0%E9%AB%98%E5%8D%8A%E5%8C%BA/image-20260131193228854.png)

Above are the explanations of PDE and PTE entries laid out in memory.

Let's waste no time and modify the code, starting with `boot.s`.

##### boot.s

```assembly
.section .text
.global _start
.type _start, @function
_start:
    mov $stack_top, %esp

    /* Key: push the address of Multiboot info structure (in ebx) onto stack for C++ */
    push %ebx
    
    call kernel_main

    cli
hltLoop:
    hlt
    jmp hltLoop
```

We can't keep `_start` in the text section, and we can't call `kernel_main` right away. We need to define a new `.scaffold` section where our entry point will be:

```assembly
/* boot.s */
.section .multiboot, "a" /* Important: this prevents the boot section from being misplaced */

.set ALIGN,    1<<0

...

.section .scaffold
.global _start
.type _start, @function
_start:
    hlt

.section .text
.global _tokernelmain
.type _tokernelmain, @function
_tokernelmain:
    mov $stack_top, %esp

    /* Key: push the address of Multiboot info structure (in ebx) onto stack for C++ */
    push %ebx
    
    call kernel_main

    cli
hltLoop:
    hlt
    jmp hltLoop
```

Without that `.multiboot` section marker, this would happen:

![image-20260131201010527](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%887%EF%BC%89%EF%BC%9A%E5%AE%9A%E6%97%B6%E4%B8%AD%E6%96%AD%E4%B8%8Esleep()%EF%BC%8C%E8%BF%81%E7%A7%BB%E5%86%85%E6%A0%B8%E5%88%B0%E9%AB%98%E5%8D%8A%E5%8C%BA/image-20260131201010527.png)

Our virtual address is 1000, but the file offset is 5008. This is because `ld` thinks we don't need to load this segment (it doesn't recognize it) and places it at the end of the file. So when QEMU boots, the multiboot header check fails.

![image-20260131201240222](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%887%EF%BC%89%EF%BC%9A%E5%AE%9A%E6%97%B6%E4%B8%AD%E6%96%AD%E4%B8%8Esleep()%EF%BC%8C%E8%BF%81%E7%A7%BB%E5%86%85%E6%A0%B8%E5%88%B0%E9%AB%98%E5%8D%8A%E5%8C%BA/image-20260131201240222.png)

After adding the marker, the offset is correct, showing 1MB.

The `linker.ld` now looks like this:

```ld
ENTRY(_start)

SECTIONS
{
	. = 1M;
    .boot BLOCK(4K) : ALIGN(4K)
    {
        *(.multiboot)
        *(.scaffold)
    }
    /* Kernel will be loaded at 1MB */
    . += 0xC0000000;

    .text ALIGN(4K) : AT(ADDR(.text) - 0xC0000000)
    {
        *(.text)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xC0000000)
    {
        *(.rodata)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - 0xC0000000)
    {
        *(.data)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - 0xC0000000)
    {
        *(COMMON)
        *(.bss)
    }
    
    _kernel_end = . - 0xC0000000;
}
```

Next, we need to define the page directory and page tables in the `.bss` section. Still a long way to go...

##### Defining Page Directory and Page Table

Let's think abstractly about how to write this.

Page directory:
We need two entries: one for virtual address space starting at 0x0 + 4MB, and one for 0xC0000000 + 4MB. Our kernel isn't that big yet, so this is sufficient.

The first goes at position 0x0/2^22 = 0, the second at position 0xC0000000/2^22 = 768.

We need PDE[0] and PDE[768] to point to the same page table (i.e., the same physical addresses), because the purpose of PDE[0] is to ensure that after enabling paging, the EIP still near 1MB can find the next instruction.

It might look like this:

```assembly
.bss
.section page_directory:
.long first page directory entry config...
.fill 767 invalid page directory entries...
.long 768th page directory entry config...
```

Page table: The page table should implement a partial flat mapping, where the base address corresponds to the same bits as the physical address it points to.

To map 4MB of space, we need to fill all 1024 page table entries.

After consulting AI, my approach was correct, but it was suggested to place them in `.data` instead of `.bss`.

```assembly
.section .data
.align 4096
page_table:
    .set i, 0
    .rept 1024
        .long (i << 12) | 0x3
        .set i, i + 1
    .endr

.section .data
.align 4096
page_directory:
    .long (page_table - 0xC0000000) + 0x3
    .fill 767, 4, 0
    .long (page_table - 0xC0000000) + 0x3
    .fill 255, 4, 0
```

I secretly did some homework with AI. Well, at least I wrote it myself first and then had AI correct it.

##### Loading CR3 and CR0

We just need to write the address of `page_directory` into CR3 and set the PG bit in CR0:

```assembly
.section .scaffold
.global _start
.type _start, @function
_start:
    cli
    movl $page_directory, %eax
    sub 0xC0000000,%eax
    movl %eax, %cr3
    movl %cr0, %eax
    or $0x80000000, %eax
    movl %eax, %cr0
    movl $_tokernelmain, %eax
    jmp *%eax

.section .text
.global _tokernelmain
.type _tokernelmain, @function
_tokernelmain:
    mov $stack_top, %esp

    push %ebx
    
    call kernel_main

    cli
hltLoop:
    hlt
    jmp hltLoop
```

After writing this code, instead of freezing on entry, it rebooted immediately.

By adding `hlt` instructions, I found that CR0 was likely the issue. After debugging, I found the `sub` instruction was the culprit. I changed it to:

```assembly
    cli
    movl $(page_directory - 0xC0000000), %eax
    movl %eax, %cr3
```

This passed. But then it rebooted again — good news, it rebooted after CR0, meaning we returned to the C function. (We missed you!)

#### Back to C

...Bad news. We don't know what's wrong yet. We're back in C code:

![image-20260131213840353](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%887%EF%BC%89%EF%BC%9A%E5%AE%9A%E6%97%B6%E4%B8%AD%E6%96%AD%E4%B8%8Esleep()%EF%BC%8C%E8%BF%81%E7%A7%BB%E5%86%85%E6%A0%B8%E5%88%B0%E9%AB%98%E5%8D%8A%E5%8C%BA/image-20260131213840353.png)

Found it — it's the rumia code here. How could it be rumia's fault? Our framebuffer memory probably isn't mapped correctly...

##### Framebuffer Remapping (Temporary)

This is a huge pit. Since we're back in C, we didn't reserve space in `.data` for framebuffer page directory entries and page tables...

We have to use a large page for now, and implement proper paging in C code later when we have `kmalloc`. This means we'll have to live with ugly code for a long time...

![image-20260131222045695](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%887%EF%BC%89%EF%BC%9A%E5%AE%9A%E6%97%B6%E4%B8%AD%E6%96%AD%E4%B8%8Esleep()%EF%BC%8C%E8%BF%81%E7%A7%BB%E5%86%85%E6%A0%B8%E5%88%B0%E9%AB%98%E5%8D%8A%E5%8C%BA/image-20260131222045695.png)

Forget it. I'm really tired today...

##### Verification

Anyway, let's print an address and see.

![image-20260131222238717](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%887%EF%BC%89%EF%BC%9A%E5%AE%9A%E6%97%B6%E4%B8%AD%E6%96%AD%E4%B8%8Esleep()%EF%BC%8C%E8%BF%81%E7%A7%BB%E5%86%85%E6%A0%B8%E5%88%B0%E9%AB%98%E5%8D%8A%E5%8C%BA/image-20260131222238717.png)

![image-20260131222226158](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%887%EF%BC%89%EF%BC%9A%E5%AE%9A%E6%97%B6%E4%B8%AD%E6%96%AD%E4%B8%8Esleep()%EF%BC%8C%E8%BF%81%E7%A7%BB%E5%86%85%E6%A0%B8%E5%88%B0%E9%AB%98%E5%8D%8A%E5%8C%BA/image-20260131222226158.png)

Regardless of our framebuffer situation, our kernel is now (virtually) living in the higher half!

---

Today, we briefly looked at the PIT, implemented the `sleep()` function, and with the mindset of "better to suffer now than later," refactored our kernel, enabled paging, and placed our kernel in the higher half of virtual address space. Our framebuffer page tables are rather hacky, but the main reason is that we haven't implemented `kmalloc` and dynamic page allocation yet! Looking forward to the future!

In the next article, we'll continue implementing physical memory management and virtual memory management! After that, we can try dynamic page allocation and get rid of the ugly code!
