## Homemade OS (13): User-Mode Processes

In the previous chapter, we wrapped up kernel-mode processes and improved many process-related features. But we're still in kernel mode.

Starting from this chapter, we'll finally step out of kernel mode and into the world of user mode.

### User Mode vs. Kernel Mode

...But why do we need user-mode processes?

You might think: user mode is safer because it can only execute non-privileged instructions and call the system calls we've prepared. Even if a user-mode process crashes, it won't bring down the entire OS. User-mode processes also have their own virtual address space, preventing contamination of kernel space. Yes! This is exactly what we've been avoiding with our kernel processes: **isolation**! Today we'll implement it.

### Infrastructure for User-Mode Process Creation

To create user-mode processes, we need to address two questions:

1. How do we ensure kernel safety — preventing user mode from reading/writing kernel space data, and preventing kernel mode from executing user space code?

2. How do we allow user mode to temporarily enter kernel mode to execute system calls, and then return to user mode?

For the first question, we add two more entries to the GDT. These entries are basically the same as the kernel mode entries (which we set up earlier). The reason we need two additional entries is that they differ from the kernel-mode entries in a critical way: they tell the CPU we're in RING3, so accessing kernel-mode data triggers a page fault.

For the second question, we set up a special software interrupt that user-mode processes can trigger to enter kernel mode. Note that we can't continue using the user-mode stack after entering kernel mode, because the user-mode ESP can be arbitrarily set and is untrusted. If the kernel mode directly uses it, a malicious user process could write to arbitrary addresses. So we need to find the current process's kernel stack. How do we find the kernel stack? The CPU does many things for us when an exception occurs — one of them is reading a data structure called TSS and loading its stored values into registers. We can pre-configure the stack-related fields in this structure so that the CPU automatically switches to the kernel stack for us when an exception occurs, and pushes the user-mode register data onto the kernel stack.

After entering the system interrupt, there are many ways to return to user mode. We'll use the `iret` instruction. When this instruction executes, the CPU also does many things for us, including restoring data from the kernel stack where we stored the user-mode data when entering kernel mode. This allows us to safely return to user mode.

Let's implement this infrastructure step by step.

#### GDT Entries

```cpp
void gdt_init() {
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFF, 0, 1);
    gdt_set_gate(2, 0, 0xFFFFF, 0, 0);
    gdt_set_gate(3, 0, 0xFFFFF, 3, 1);
    gdt_set_gate(4, 0, 0xFFFFF, 3, 0);

    load_gdtr();
}
```

It turns out we already prepared user-mode code and data segments when setting up the GDT earlier, with DPL correctly set to 3. So we can directly set up the TSS now.

```cpp
void gdt_set_tss(int32_t num, uint32_t base, uint32_t limit) {
    gdt_entry_struct *entry = &gdt_entries[num];

    entry->base_low    = (base & 0xFFFF);
    entry->base_middle = (base >> 16) & 0xFF;
    entry->base_high   = (base >> 24) & 0xFF;

    entry->limit_low   = (limit & 0xFFFF);
    entry->limit_high  = (limit >> 16) & 0x0F;

    // Access byte: present=1, DPL=0, descriptor_type=0 (system segment), type=0x9
    entry->present         = 1;
    entry->dpl             = 0;
    entry->descriptor_type = 0;  // System segment
    entry->executable      = 1;  // type bit 3 = 1
    entry->conforming_expand = 0;  // type bit 2 = 0
    entry->read_write      = 0;  // type bit 1 = 0
    entry->accessed        = 1;  // type bit 0 = 1
    // type = 1001b = 0x9 = 32-bit available TSS

    // Flags
    entry->granularity     = 0;  // Byte granularity
    entry->default_size    = 0;  // This bit is 0 in TSS
    entry->long_mode       = 0;
    entry->available       = 0;
}

...

void load_tr() {
    asm volatile(
        "mov $0x28, %ax \n"
        "ltr %ax"
    );
}

void tss_set_kernel_stack(uint32_t esp) {
    tss.esp0 = esp;
}

void tss_init(uint32_t kernel_ss, uint32_t kernel_esp) {
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = kernel_ss;
    tss.esp0 = kernel_esp;
    tss.iomap_base = sizeof(tss_entry);
}

void gdt_init() {
    tss_init(0x10, 0);  // esp0 will be updated by scheduler later
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFF, 0, 1);
    gdt_set_gate(2, 0, 0xFFFFF, 0, 0);
    gdt_set_gate(3, 0, 0xFFFFF, 3, 1);
    gdt_set_gate(4, 0, 0xFFFFF, 3, 0);
    gdt_set_tss(5, (uint32_t)&tss, sizeof(tss) - 1);
    load_gdtr();
    load_tr();
}
```

Most of this configuration was written by Claude, because I don't want to dig into descriptor formats... I'm here to write an OS, not to become a descriptor lawyer.

Note that we use `tss_set_kernel_stack` to allow the scheduler to update the kernel stack value in the TSS when scheduling.

```cpp
void update_kernel_stack(uint32_t esp);
```

We expose this in `hal.h` as `update_kernel_stack`, reminding people (especially the scheduler) to update the kernel stack when switching processes.

```cpp
    chosen_process->state = process_state::RUNNING;
    update_kernel_stack((uint32_t)chosen_process->kernel_stack_bottom + KERNEL_STACK_SIZE);
    process_switch_to(chosen_process->pid);
}
```

Note that we store the bottom of the kernel stack here, implying that the kernel stack will always be empty when entering user mode.

#### System Call Interrupt

We register an interrupt with number 0x80:

```cpp
idt_set_gate(0x80, (uint32_t)(&system_call_handler), 0x08, 3);
```

I've somewhat forgotten what the fields above mean. The main thing is the last two parameters: `0x08` indicates this is a kernel code segment function, and `3` means ring 3 can trigger this interrupt via `int 0x80`.

For `system_call_handler`, we first write a trampoline in assembly to save the user's registers onto the kernel stack, then jump to a C++ handler function. Writing directly in C could cause our registers to be corrupted.

```assembly
.extern inner_syscall_handler
.global system_call_handler
    
system_call_handler:
    # SS, ESP, EFLAGS, CS, EIP are automatically saved during privilege level switch
    pushl %es
    pushl %ds
    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax

    mov $0x10, %eax
    mov %eax, %ds
    mov %eax, %es

    pushl %esp
    call inner_syscall_handler
    
    addl $4, %esp
    popl %eax
    popl %ebx
    popl %ecx
    popl %edx
    popl %esi
    popl %edi
    popl %ebp
    popl %ds
    popl %es
    iret
```

C++:

```cpp
typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t ds;
    uint32_t es;

    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp;
    uint32_t ss;
} interrupt_frame;

typedef uint32_t (*syscall_handler_t)(interrupt_frame*);
void inner_syscall_handler(interrupt_frame* reg);
void register_syscall(uint8_t n, syscall_handler_t handler);
```

Next, we write a function in libc for invoking system calls and passing arguments:

```cpp
#ifndef _SYSCALL_DEF_H
#define _SYSCALL_DEF_H 1
#include <stdint.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif
enum class SYSCALL {
    EXIT = 0,
    TERMINAL_WRITE = 1
};


static inline uint32_t syscall0(uint32_t num) {
    uint32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num));
    return ret;
}

static inline uint32_t syscall1(uint32_t num, uint32_t arg1) {
    uint32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1));
    return ret;
}

static inline uint32_t syscall2(uint32_t num, uint32_t arg1, uint32_t arg2) {
    uint32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2));
    return ret;
}

#ifdef __cplusplus
}
#endif

#endif

```

We can include this header file in the kernel to unify SYSCALL types:

```cpp
#include <kernel/syscall.h>
#include <syscall_def.h>

constexpr uint32_t MAX_SYSCALL = 255;
syscall_handler_t syscall_table[255] = { nullptr };

void register_syscall(uint8_t n, syscall_handler_t handler) {
    syscall_table[n] = handler;
}

void inner_syscall_handler(interrupt_frame* reg) {
    uint32_t syscall_num = reg->eax;
    if (syscall_num >= MAX_SYSCALL || !syscall_table[syscall_num]) {
        reg->eax = (uint32_t)(SYSCALL_RET::SYSCALL_NOT_FOUND);
        return;
    }
    reg->eax = (syscall_table[syscall_num])(reg);
}
```

I don't want to write a bunch of switch-case statements here. Like our IRQ registration mechanism, I need a registration function here. Functions that want to handle system calls register themselves, and `inner_syscall_handler` just looks up the table.

That's enough for system call infrastructure. Once the groundwork is laid, setting up system calls is relatively simple.

#### User-Mode Virtual Address Space

We need to provide an interface that gives each user-mode process a new virtual address space, where addresses below `0xC0000000` are user-accessible, and addresses above `0xC0000000` are kernel-exclusive. This means creating a new page directory, copying the virtual mappings above `0xC0000000` from the kernel page directory, allocating enough virtual address space, and copying the user's code there along with the code size parameter.

But our VMM was designed to only work with the currently set CR3. So what do we do? We add a function to switch CR3. Before switching, disable interrupts. After switching, frantically copy page table entries, add entries, and copy code... Then switch back to the kernel's CR3 and re-enable interrupts. That should work.

##### Creating a User Process Interface
Let's start from a high level. Following the above approach, let's design and implement the `create_user_process` interface:

```cpp
uint32_t create_user_process(void* code, uint32_t code_size, uint8_t priority);
```

I've prepared the implementation code. Let's explain it block by block, top to bottom.

```cpp
uint32_t create_user_process(void* code, uint32_t code_size, uint8_t priority) {
    spinlock_acquire(&process_list_lock);
```

Here, along with the `create_process` below, I've added a lock for accessing `process_list`. I'm not sure if this is necessary, but for safety, I've done it anyway.

```cpp
    uint8_t newpid = 0;
    for (auto nid = 1; nid < MAX_PROCESSES_NUM; ++nid) {
        if (process_list[nid] == nullptr) {
            newpid = nid;
            break;
        }
    }
    if (newpid == 0) {
        spinlock_release(&process_list_lock);
        return 0;
    }
```

We can never be assigned PID 0, same logic as before for kernel processes.

```cpp
    uint32_t pd_addr_old = vmm_get_cr3();
    uint32_t pd_addr = vmm_create_page_directory();
```

Note that `pd_addr` holds the physical address of our page table, which will be passed when switching CR3 later.

```cpp
    asm volatile ("cli");
    vmm_switch(pd_addr);
    uint32_t pages_needed = (code_size + 4095) / 4096;
    for (uint32_t i = 0; i < pages_needed; i++) {
        void* phys = pmm_alloc(1);
        vmm_map_page((uintptr_t)phys, CODE_SPACE_ADDR + i * 4096, 6);
    }
    memcpy((void*)CODE_SPACE_ADDR, code, code_size);
```

Here we allocate space for the user-mode code, set up virtual address mappings, and copy the kernel-mode code over.

```cpp
    void* stack_space = pmm_alloc(1);
    vmm_map_page(reinterpret_cast<uintptr_t>(stack_space), CODE_STACK_TOP_ADDR, 6);
```

Here we allocate space for the user stack. We can construct the stack frame here for passing parameters later, but we'll skip this logic for now and come back to it.

```cpp
    PCB*& new_process = process_list[newpid];
    new_process = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
    memset(new_process, 0, sizeof(PCB));
    new_process->kernel_stack_bottom = kmalloc(KERNEL_STACK_SIZE);
    new_process->esp = (uintptr_t)(new_process->kernel_stack_bottom) + KERNEL_STACK_SIZE;
    
    // Kernel stack
    *((uintptr_t*)(new_process->esp - 4)) = 0x23; // SS
    *((uintptr_t*)(new_process->esp - 8)) = CODE_STACK_TOP_ADDR + 4096; // ESP
    *((uintptr_t*)(new_process->esp - 12)) = 0x202; // EFLAG
    *((uintptr_t*)(new_process->esp - 16)) = 0x1B; // CS
    *((uintptr_t*)(new_process->esp - 20)) = CODE_SPACE_ADDR; // EIP
    *((uintptr_t*)(new_process->esp - 24)) = reinterpret_cast<uintptr_t>(&ret_to_user_mode);
    *((uintptr_t*)(new_process->esp - 28)) = 0x200;  // EFLAGS (popfl)
    *((uintptr_t*)(new_process->esp - 32)) = 0;      // ebx
    *((uintptr_t*)(new_process->esp - 36)) = 0;      // esi
    *((uintptr_t*)(new_process->esp - 40)) = 0;      // edi
    *((uintptr_t*)(new_process->esp - 44)) = 0;      // ebp  ← stack top, popped first
    new_process->esp -= 44;
    new_process->pid = newpid;
    new_process->create_time = pit_get_ticks();
    new_process->cr3 = pd_addr;
    new_process->state = process_state::READY;
    insert_into_scheduling_queue(newpid, priority);
```

Here we allocate kernel stack space for the process and construct the kernel stack frame. Near the top of the stack, the registers we construct for the scheduler's process switch are the same as when creating a kernel process. But note that below that, we don't directly pass the process function's entry point — instead, we call a special trampoline function to switch ds and es to user space, then execute `iret` to "return" to user mode. The implementation of `ret_to_user_mode` is as follows:

```assembly
ret_to_user_mode:
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    iret
```

As we mentioned before, `iret` automatically restores the five registers SS, ESP, EFLAGS, CS, and EIP. We need to construct the stack frame for `iret` restoration on the kernel stack.

```cpp
    vmm_switch(pd_addr_old);
    asm volatile ("sti");
    spinlock_release(&process_list_lock);

    return newpid;
}
```

After preparing everything, we switch CR3 back, enable interrupts, release the process_list lock, and return the user-mode process's ID.

Now let's talk about the new VMM-related functions.

##### New VMM Interfaces

```cpp
static inline uintptr_t vmm_get_cr3() {
    uintptr_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void vmm_switch(uint32_t cr3) {
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}
```

Simple assembly instructions, wrapped as functions.

```cpp
uintptr_t vmm_create_page_directory() {
    constexpr uint32_t TEMP_PD_ADDR = 0xCF000000;
    uintptr_t p_addr = reinterpret_cast<uintptr_t>(pmm_alloc(1));

    vmm_map_page(p_addr, TEMP_PD_ADDR, 3);

    PDE* kernel_pde_list = reinterpret_cast<PDE*>(page_directory);
    PDE* cur_pde_list = reinterpret_cast<PDE*>(TEMP_PD_ADDR);
    memset(cur_pde_list, 0, sizeof(cur_pde_list));
    for (uint16_t i = 0; i < 1023; ++i) {
        cur_pde_list[i] = kernel_pde_list[i];
    }
    cur_pde_list[1023] = {0};
    cur_pde_list[1023].frame = p_addr >> 12;
    cur_pde_list[1023].read_write = 1;
    cur_pde_list[1023].present = 1;

    vmm_unmap_page(TEMP_PD_ADDR);
    return p_addr;
}
```

We directly allocate a physical page, manually set up virtual address mappings, copy the current page table (just like when we built the kernel page table), and fill entry 1023 with the page directory's physical address. This ensures that regardless of whether we're in kernel or user mode, the virtual address of the page directory is always fixed.

One thing worth noting: I'm copying the entire page table here, not just the last 1GB of virtual address space. This is because I can't quite get rid of some low-address stuff yet — I need to remap them. But this isn't critical (it just wastes a small amount of low address space). We'll clean this up later.

With the infrastructure ready, let's jump into practice.

### A Simple User-Mode Program

Let's write a simple user-mode program:

```cpp
void user_program() {
    while(1) {}
}

...
create_user_process(reinterpret_cast<void*>(&user_program), 4096, 1);
```

For convenience, I've hardcoded the loaded memory size to 4KB.

![image-20260226105416538](../assets/自制操作系统（13）：用户态进程/image-20260226105416538.png)

Let's do a simple experiment to determine if this is actually a user-mode program.

```cpp
void user_program() {
    asm volatile ("hlt");
    while(1) {}
}
```

We make this program halt the CPU.

![image-20260226105527434](../assets/自制操作系统（13）：用户态进程/image-20260226105527434.png)

We can see that the `hlt` instruction triggered a GPF (General Protection Fault)! This tells us the instruction we executed was privileged — we've finally entered user mode!

#### Testing System Calls

```cpp
uint32_t sys_exit(interrupt_frame*) {
    exit_process(cur_process_id);
    return 0;
}

uint32_t sys_terminal_write(interrupt_frame* reg) {
    terminal_write((const char*)reg->ebx , strlen((const char*)reg->ebx));
    return 0;
}

void syscall_init() {
    register_syscall(uint32_t(SYSCALL::EXIT), sys_exit);
    register_syscall(uint32_t(SYSCALL::TERMINAL_WRITE), sys_terminal_write);
}
```

We registered some system calls. Let's have our user-mode program invoke them:

```cpp
void user_program() {
    const char* s = "hello user space!";
    syscall0((uint32_t)SYSCALL::EXIT);
}
```

![image-20260226115132033](../assets/自制操作系统（13）：用户态进程/image-20260226115132033.png)

It crashed after running.

```cpp
void user_program() {
    uint32_t ret;
    uint32_t num = 0;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num));
}
```

![image-20260226115302922](../assets/自制操作系统（13）：用户态进程/image-20260226115302922.png)

Switched to inline assembly, and it exited normally. Calling a function causes a crash, which makes me suspect it's a user stack issue.

After using GDB, I found that directly breaking on `user_program` didn't work. I guessed that user space had remapped our program, so I used `break *0x10000000` (the address where our user-space code is loaded). Success:

![image-20260226115751880](../assets/自制操作系统（13）：用户态进程/image-20260226115751880.png)

But there's no symbol table, so we can only see assembly.

![image-20260226120203656](../assets/自制操作系统（13）：用户态进程/image-20260226120203656.png)

Oh! Our `syscall` function is still over in kernel space — it hasn't been copied over! This is the trouble with not compiling into independent files. We have no choice but to use inline assembly for now:

```cpp
void user_program() {
    uint32_t ret;
    uint32_t num = 0;
    const char* s = "hello from user space!\n";
    asm volatile("int $0x80" : "=a"(ret) : "a"(1), "b"(s));
    asm volatile("int $0x80" : "=a"(ret) : "a"(0));
}
```

![image-20260226120802783](../assets/自制操作系统（13）：用户态进程/image-20260226120802783.png)

Regardless, we can now successfully use system calls for console printing and process exiting!

---

### Summary

In this chapter, we successfully built the infrastructure for user-mode processes, moved a simple function to user space, and achieved the first "Hello world" from a user program in our OS. Of course, our journey can't stop at Hello world. Next, we'll independently compile user-mode binary programs. See you in the next chapter!
