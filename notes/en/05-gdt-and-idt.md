## Homemade OS (5): GDT and IDT

In the previous article, we improved the console logic and acquired a usable set of console output functions, which laid the foundation for our subsequent debugging.

Now, let's add a simple line of code to `kernel_main` to test what happens:

```cpp
printf("1 / 0 = %d\n", 1 / 0);
```

![image-20260129140637680](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129140637680.png)

As you can see, our system is defeated by this tiny division by zero! It keeps rebooting and can't enter the system at all!

This is ridiculous, but the root cause is that we haven't properly set up the Interrupt Descriptor Table (IDT). Today, we'll focus on IDT and the related GDT to improve our operating system.

### GDT (Global Descriptor Table)

To set up the IDT, we should first set up our own GDT.

And to talk about GDT, we have to talk about the segmentation mechanism.

#### Segmentation Mechanism

What is segmentation? Simply put, when we write assembly instructions to access certain addresses, we're not actually accessing the address we wrote. That address is just an offset — an offset relative to a base address set in a segment register. For example, for the following assembly instruction:

```assembly
mov eax, [$0x10]
```

We're not writing the content at `$0x10` into register `eax`. For this `mov` instruction (which reads data), we're reading the content from the physical address computed as: the base address selected by the data segment register (`ds`) plus this offset value (`0x10`), then storing it into `eax`.

For instance, if the base address selected by `ds` is `0x90000000`, then the physical address we actually access (in protected mode) is `0x90000000 + 0x10 = 0x90000010`.

So, does the `ds` register directly store a base address? In real mode, yes. But with GRUB's help during boot, we go directly to protected mode. The `ds` register stores a **segment selector**, which selects a segment descriptor in the GDT, and it's inside that segment descriptor that the actual base address for the offset is stored...

Sounds confusing, right? That's because this isn't something that can be explained in just a few sentences. Readers will need to do some additional reading to clarify this topic. At the end of this article, I'll attach some references that I personally found helpful. **For now, just remember that we need to set up a table in memory, insert some entries representing base addresses into it, and let our segment registers select from it.**

#### Setting Up the GDT

Actually, GRUB already sets up a GDT for us during boot. But since GRUB sets it up, and something this important must be under our control, we need to set up our own GDT — cutting the "umbilical cord" with GRUB and taking fate into our own hands. This is also why, before setting up the IDT, we should first set up our own GDT.

To set up the GDT, we need to tell the CPU where our GDT is located, its size, and populate the entries in the table.

Let's start by setting up the table entries. Since GDT is the Global Descriptor Table, the entries in the table are global descriptors (segment descriptors). Here's the format of a segment descriptor:

![image-20260129164328809](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129164328809.png)

```c
struct gdt_entry_struct {
    uint16_t limit_low;           // Segment limit (bits 0-15)
    uint16_t base_low;            // Segment base (bits 0-15)
    uint8_t  base_middle;         // Segment base (bits 16-23)

    /* Access Byte: defines segment access permissions (low to high bits) */
    uint8_t accessed : 1;         // Whether accessed (usually 0)
    uint8_t read_write : 1;       // Code: readable / Data: writable
    uint8_t conforming_expand : 1;// Code: conforming / Data: expand-down
    uint8_t executable : 1;       // 1=code segment, 0=data segment
    uint8_t descriptor_type : 1;  // 1=code/data, 0=system (e.g., TSS)
    uint8_t dpl : 2;              // Privilege level (0=kernel, 3=user)
    uint8_t present : 1;          // Segment present (must be 1)

    /* Flags + Limit High: combined byte (low to high bits) */
    uint8_t limit_high : 4;       // Segment limit (bits 16-19)
    uint8_t available : 1;        // Available for software (usually 0)
    uint8_t long_mode : 1;        // 1=64-bit mode (0 for 32-bit)
    uint8_t default_size : 1;     // 1=32-bit protected mode, 0=16-bit
    uint8_t granularity : 1;      // 1=4KB unit, 0=1 byte unit

    uint8_t  base_high;           // Segment base (bits 24-31)
} __attribute__((packed));
```

Our GDT will contain 5 such entries: Null segment, Kernel Code segment, Kernel Data segment, User Code segment, and User Data segment.

##### HAL

To facilitate decoupling from specific architectures, we'll create a `hal.h` file and define a function `hal_init()` in it, which different architectures can use to perform hardware-specific initialization. Here, we can initialize our GDT within the implementation of this function.

![image-20260129182022531](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129182022531.png)

Five segments, configured according to the above description. By the way, this configuration is called "flat mode" because our base address is 0, so the offset effectively becomes our linear address (linear address = base + offset).

With these settings in place, we can pass the table information to the `gdtr` register so it can load our custom GDT. Can we just assign the address directly to this register? No — we need to construct a structure containing the base address and size of the GDT table, then assign it to `gdtr`. The structure looks like this:

![image-20260129183021471](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129183021471.png)

In our code, it looks like this:

![image-20260129185348459](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129185348459.png)

![image-20260129185404908](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129185404908.png)

Then we load it using inline assembly. I don't actually know inline assembly — I wrote this through a combination of "reciting from memory" and understanding. Hopefully I'll get better at it over time.

![image-20260129185538684](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129185538684.png)

Here I've commented out the division-by-zero statement and added some print output. It looks like the GDT has been loaded successfully. Now let's continue setting up the IDT.

### IDT (Interrupt Descriptor Table)

Finally, we're getting to the main topic. We've been preparing all of this just to set up the IDT. But why do we need to set up the IDT? Can the IDT tell us what 1 divided by 0 equals? (Fun fact: No, it cannot.)

The IDT actually stores a series of entries that specify "which function should handle a particular interrupt when it occurs." Among these interrupts is the "division by zero exception."

If we don't properly set up how to handle exceptions when they occur, the CPU will be overwhelmed and不知所措 (at a loss)...

So today's task is to set up this table, along with the entry for handling the division-by-zero exception!

#### Setting Up the IDT

Similar to our GDT, the IDT is also a table containing multiple descriptor entries. There's also an `idtr` register through which we can tell the CPU where our IDT is located.

![image-20260129194405003](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129194405003.png)

The description of each IDT entry is shown above. The initialization process is quite similar to the GDT — set up each entry, fill in the IDT table selector, and load it into the register using the `lidt` command.

![image-20260129194606779](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129194606779.png)

#### The Trampoline

The part highlighted in the red box above needs some explanation. Some might ask: "Couldn't we just pass a function pointer here? Why not just define an `interrupt_handler` function and pass it directly?"

When an interrupt occurs, the CPU automatically pushes some registers (EIP, etc.) onto the stack. If there's an error code, it will also be passed as a parameter to the interrupt handler. After the handler finishes processing, to restore the state before the interrupt, we need to use the `iret` instruction to restore these registers. However, C functions use the `ret` instruction to return. So we need an assembly-written function to serve as a trampoline — it jumps to our C function, and then the assembly function restores the context.

We can have all interrupts jump to our internal C function for handling. But not all interrupts carry an error code, so we need to balance the two types. For interrupts without an error code (like our current division-by-zero interrupt), we push a dummy error code of 0 to eliminate the difference.

![image-20260129204723545](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129204723545.png)

Here we first define the division-by-zero interrupt handler routine.

![image-20260129204819340](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129204819340.png)

After declaring it as external, we can write it like this.

Note in the assembly code above, we push a series of registers along with parameters to the C function `inner_interrupt_handler`:

![image-20260129212635917](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129212635917.png)

![image-20260129212702303](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129212702303.png)

Now we can catch the exception. (What a pain! I don't know if it's the compiler or something else, but the division-by-zero interrupt triggered here was actually interrupt number 6!)

![image-20260129212822576](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129212822576.png)

Of course, triggering it directly also catches interrupt 0. A bit annoying.

#### Capturing More Software Interrupts

We've captured interrupts 0 and 6 so far. For software interrupts, we need to capture the remaining 30 as well. Their assembly trampoline functions all look very similar, so we use macros to define a template and generate all these trampoline functions in batch, reducing the chance of errors.

#### Setting Colors

Later, I also added text color setting logic to the console:

![image-20260129214717730](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%885%EF%BC%89%EF%BC%9AGDT%E4%B8%8EIDT/image-20260129214717730.png)

Errors are now more noticeable (and scarier!).

---

Today we set up our own GDT and IDT, gaining a certain level of interrupt handling capability.

In the next section, we'll look at how to handle keyboard interrupts and display keyboard input on screen.
