## Homemade OS (11): Process Creation, Scheduling and Switching (Part 1)

In the previous chapters, we successfully implemented memory management modules in our operating system. We can now allocate arbitrary heap space. Now, let's take a bold step forward — let's look at how to implement multi-process.

### The Essence of Multi-Process

To talk about multi-process, we first need to talk about what a process is. In simple terms, a process is a running program. A program on our hard drive is just a simple file. We read it into memory following its rules, point our "next instruction to execute" to the program's entry point, and then the program's instructions, working with its data, can run on the CPU. That's what makes a process. Our current kernel process is exactly like this.

The essence of multi-process is time-division multiplexing. A CPU can only execute one instruction at a time, but by rapidly switching between different processes, it appears as if multiple processes are running simultaneously, with each process feeling like it has exclusive access to the CPU, memory, and other resources. We're still in kernel mode right now, so in this chapter we'll first implement kernel process creation and scheduling, rather than isolation. Isolation is something we'll implement when we create user-mode processes in the future. This is because kernel code is written by us, and we should rely on the code itself (through proper engineering) to ensure reliability and safety, not isolation. However, we can briefly talk about future **isolation**:

The exclusive experience mentioned above relies on our **virtualization** of hardware resources. Think about it — we went through so much trouble to set up the higher-half kernel, virtual memory, paging... Why not just use physical memory directly? Beyond fragmentation management, there's a very important reason: by switching different page directories (changing CR3), the abstraction of virtual addresses makes each process feel like it has exclusive access to all of memory. Amazing, isn't it? Virtualization is the means; making each process feel like it's exclusively owning all hardware resources without interfering with each other during execution (i.e., "isolation") is the goal.

How do we prevent them from interfering with each other? Take our current kernel process — we've only had one process from start to finish, so it does indeed have exclusive access to all hardware resources. Now, suppose we add another process. To keep them from affecting each other, we need to temporarily transfer existing hardware resources to this new process. This transfer involves saving the old data on these resources and restoring it when transferring back. All of this "old data" that needs to be saved and restored is called the **"process context"**, which includes registers and memory. Since we've already virtualized memory (can switch virtual memory with a CR3 change), overall, we just need to save the register data.

#### Interface

Let's first look at the basic interfaces for process creation and scheduling:

```cpp
uint32_t create_process(void* entry);
uint32_t exit_process(uint32_t pid);
```

And the PCB (Process Control Block) structure that records process context:

```cpp
struct PCB {
    uint32_t pid;

    // General registers
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;

    // Stack and execution position
    uint32_t esp;
    uint32_t ebp;
    uint32_t eip;

    // Flags register
    uint32_t eflags;

    // This task's kernel stack bottom (for freeing memory)
    uint32_t kernel_stack_bottom;
};
```

We organize all PCB pointers into an array for unified management:

```
static PCB* process_list[MAX_PROCESSES_NUM];
static uint8_t cur_process_idx;
```

For ease of explanation, let's also create a process scheduling-related file: `schedule.h`, defining a function `yield()` that indicates a process is willing to voluntarily yield hardware resources (switch to another process).

```cpp
void yield();
```

#### Process Logic Initialization

For our existing single kernel main process, after we implement process creation logic later, these newly created processes will need to return to our original kernel main process when they exit. This requires us to wrap the original main process into a process as well. Let's make this process 0.

```
.global stack_bottom
```

```cpp
extern uintptr_t stack_bottom;

void process_init() {
    process_list[0] = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
    process_list[0]->kernel_stack_bottom = reinterpret_cast<void*>(stack_bottom);
    process_list[0]->pid = 0;
    cur_process_id = 0;
}


uint32_t exit_process(uint32_t pid) {
    if (pid == 0) return 1;
}
```

I think exiting process 0 should be treated as an exception and return an error, otherwise we'd have trouble later if we try to `kfree` the kernel stack that was originally in the `.bss` section.

#### Creating a Process

To create a process, in theory, we need: an address representing the process entry point for jumping to, an initialized virtual memory space for the process to use, and initialized register states for initializing the process state. However, for kernel processes, we don't need to initialize memory space. This is because generally, our program files have multiple sections — `.bss` (uninitialized static data that needs zeroing), `.data` (static data with specific initial values), `.rodata` (read-only immutable data), `.text` (executable code instructions). For kernel processes, we've been using the same virtual memory all along, and all these sections were loaded into memory during the boot phase. Therefore, we only need to provide an entry point to create a kernel process. Switching to the corresponding entry point effectively switches kernel processes — very simple.

```cpp
uint32_t create_process(void* entry) {
    for (auto nid = 0; nid < MAX_PROCESSES_NUM; ++nid) {
        if (process_list[nid] == nullptr) {
            PCB*& new_process = process_list[nid];
            new_process = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
            memset(new_process, 0, sizeof(PCB));
            new_process->kernel_stack_bottom = kmalloc(4096);
            new_process->esp = (uintptr_t)(new_process->kernel_stack_bottom);
            new_process->ebp = (uintptr_t)(new_process->kernel_stack_bottom);
            new_process->eip = (uintptr_t)(entry);
            new_process->pid = nid;
            process_switch_to(nid);
            return nid;
        }
    }
    return 0;
}
```

In `create_process`, we simply set the corresponding PCB fields: find an empty slot in `process_list`, allocate memory for the PCB and zero it out, allocate another block of memory as its kernel stack, set the stack pointer, set EIP to the function's entry point address, set the PID, and return the PID.

Note the `process_switch_to` call — after creating a process, we should switch the currently running process to this new process. We'll leave the implementation details for the context switching section later.

#### Exiting a Process

Exiting a process is essentially destroying it — freeing the allocated PCB memory and kernel stack memory, and setting the corresponding PCB list entry to null.

```cpp
uint32_t exit_process(uint8_t pid) {
    if (pid == 0 || process_list[pid] == nullptr) return 1;
    PCB*& cur_process = process_list[pid];
    kfree(reinterpret_cast<void*>(cur_process->kernel_stack_bottom));
    kfree(reinterpret_cast<void*>(cur_process));
    cur_process = nullptr;
    yield();
    // Should never reach here
    return 0;
}
```

#### Context Switching

We've created processes and saved their information in `process_list`. But how do we make our processes "execute one after another"?

##### A Beautiful World

Imagine a cooperative world where every process is willing to voluntarily call `yield` after doing a certain amount of work to yield the CPU. The flow would be: Process 1 runs for a while, voluntarily yields → the scheduler selects another process (Process 2) to continue → that process runs for a while, yields → ... until all processes complete their tasks.

So perhaps we could implement `yield` like this:

```cpp
void yield() {
    for (uint8_t i = 1; i < MAX_PROCESSES_NUM; ++i) {
        if (i == cur_process_id) continue;
        if (process_list[i]) {
            process_switch_to(i);
        }
    }
    // If no other process can be switched to, go back to process 0
    process_switch_to(0);
    // Should never reach here
    panic("failed to switch to process 0");
}
```

This simple implementation might work well enough. Now let's implement the `process_switch_to` function.

##### process_switch_to

`process_switch_to` deserves special attention. Explaining what `process_switch_to` should do isn't difficult — save the current register state to the current process's PCB, then load the corresponding PCB's register state. However, in actual implementation, we run into the problem that we can't directly use `mov EIP, ...`. We have two approaches: one is to push the entry address onto our kernel stack and then execute a `ret` instruction, which makes us "return" to the function's entry point; the other is to `jmp` to the entry address. Comparing them, the difference is just one of conciseness vs. intuitiveness. We'll go with the approach of saving registers on the stack, which gives us an additional convenience: we only need to save the ESP register in the PCB.

Since we're changing EIP by executing a fake `ret`, let's see what `ret` actually does:

```
pop eip    ; Pop the value esp points to into eip, then esp += 4
```

This means we can use assembly instructions to set `esp` to the value in the corresponding PCB block's `esp`, use `pop` to restore registers, and finally use `ret` to jump to the corresponding entry function (or rather, the place where the process last yielded). This requires us to prepare the new process's kernel stack frame during the `create_process` phase. So we need to update the PCB and `create_process`:

```cpp
typedef struct PCB {
    uint8_t pid;
    uintptr_t esp;
    // This task's kernel stack bottom (for freeing memory)
    void* kernel_stack_bottom;
} PCB;

uint32_t create_process(void* entry) {
    for (auto nid = 0; nid < MAX_PROCESSES_NUM; ++nid) {
        if (process_list[nid] == nullptr) {
            PCB*& new_process = process_list[nid];
            new_process = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
            memset(new_process, 0, sizeof(PCB));
            new_process->kernel_stack_bottom = kmalloc(4096);
            new_process->esp = (uintptr_t)(new_process->kernel_stack_bottom) + 4096;
            *((uintptr_t*)((new_process->esp - 4))) = reinterpret_cast<uintptr_t>(&exit_process_wrapper);
            *((uintptr_t*)(new_process->esp - 8)) = reinterpret_cast<uintptr_t>(entry);
            *((uintptr_t*)(new_process->esp - 12)) = 0; // ebx
            *((uintptr_t*)(new_process->esp - 16)) = 0; // esi
            *((uintptr_t*)(new_process->esp - 20)) = 0; // edi
            *((uintptr_t*)(new_process->esp - 24)) = 0; // ebp
            new_process->esp -= 24;
            new_process->pid = nid;
            process_switch_to(nid);
            return nid;
        }
    }
    return 0;
}
```

Notice that I also pushed an `exit_process_wrapper` function to exit the current process. Otherwise, after our process's `ret`, it would either jump to some unknown location and crash, or crash immediately. The function is implemented as:

```cpp
void exit_process_wrapper() {
    exit_process(cur_process_id);
}
```

Now we can implement `process_switch_to` in assembly:

```assembly
.extern process_list
.extern cur_process_id
.global process_switch_to

process_switch_to:
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ebp

    #save...
    movzbl cur_process_id, %eax
    movl process_list(, %eax, 4), %eax
    movl %esp, 4(%eax)

    #update cur_process_id and esp register...
    movzbl 20(%esp), %eax
    movl process_list(, %eax, 4), %eax
    movl (%eax), %ebx
    movb %bl, cur_process_id
    movl 4(%eax), %esp

    popl %ebp
    popl %edi
    popl %esi
    popl %ebx

    ret
    
```

(The short assembly code above took me a long time to write...)

Let's not waste any time — let's create two functions representing two independent processes:

```cpp
void proc1() {
    for (uint32_t i = 0; i < 99; i++) {
        printf("proc1:%d/99\n", i);
        yield();
    }
}

void proc2() {
    for (uint32_t i = 0; i < 99; i++) {
        printf("proc2:%d/99\n", i);
        yield();
    }
}

...kernel_main
    process_init();
    asm volatile ("sti");
    printf("OK\n");
    printf("Welcome, aoverb!\n\n");
    printf("The kernel_main lies in %X, sounds great!\n\n", &kernel_main);
    char input[256];
    
    create_process(reinterpret_cast<void*>(&proc1));
    create_process(reinterpret_cast<void*>(&proc2));
...
```

![image-20260223195658184](../assets/自制操作系统（11）：进程创建、调度与切换（上）/image-20260223195658184.png)

Uh oh... there's a problem...

![image-20260223200217517](../assets/自制操作系统（11）：进程创建、调度与切换（上）/image-20260223200217517.png)

Later I realized that I couldn't write it that way...

![image-20260223200152508](../assets/自制操作系统（11）：进程创建、调度与切换（上）/image-20260223200152508.png)

It works! Now we can see Process 1 and Process 2 running side by side, alternating execution! We've finally taken the exciting first step into multi-processing! 

##### The Real World

Now let's calm down a bit and return to the real world — processes won't be so cooperative with each other. Some might not want to yield CPU resources at all, or in more complex situations, a process might not be able to yield the CPU. What do we do then? Our current scheduling algorithm is practically non-existent — we need a better scheduling algorithm. We'll cover this in the next part.
