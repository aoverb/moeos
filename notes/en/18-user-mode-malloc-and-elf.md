## Homemade Operating System (18): User-Mode malloc/free and ELF Loading

Our process doesn't support heap space yet! This means we're still reading program images on the stack...

If we keep doing this, larger programs will put the stack space in danger. We need to implement heap space for processes.

### Recording Heap Pointers in the PCB

```cpp
typedef struct PCB {
    pid_t pid;
    uintptr_t esp;
    uintptr_t cr3;
    uint32_t saved_eflags;
    // Kernel stack bottom for this task (used for memory deallocation)
    void* kernel_stack_bottom;
    uintptr_t heap_start;  // Heap starting address (fixed, never changes)
    uintptr_t heap_break;  // Current heap top (sbrk moves this)

    uint16_t priority;
    uint16_t quota;
    uint32_t create_time;

    PCB* prev = nullptr;
    PCB* next = nullptr;

    pid_t parent_pid;
    uint8_t to_exit;
    int exit_code;
    process_state state;
    process_queue waiting_queue;

    file_description fd[MAX_FD_NUM];
    uint32_t fd_num;

    char cwd[256];
} PCB;
```

For the logic of initializing these two values when creating a process, we now have to take `USER_ADDR_SPACE` and add a size to set the heap starting address:

```cpp
pid_t create_user_process(void* code, uint32_t code_size, uint8_t priority, int argc, char** argv) {
    ...
    vmm_switch(pd_addr);
    uint32_t pages_needed = (code_size + 4095) / 4096;
    for (uint32_t i = 0; i < pages_needed; i++) {
        void* phys = pmm_alloc(1);
        vmm_map_page((uintptr_t)phys, CODE_SPACE_ADDR + i * 4096, 6);
    }
    memcpy((void*)CODE_SPACE_ADDR, code_buf, code_size);
    kfree(code_buf);
    ...
    new_process->heap_start = (CODE_SPACE_ADDR + code_size + 0xFFF) & ~0xFFF // page-aligned
    new_process->heap_break = new_process->heap_start; // heap size starts at 0
    ...
}
```

### The sbrk System Call

`sbrk` stands for "set break" — "break" is the Linux term for the end of the code/data segment. Each time we call `sbrk`, we incrementally increase the heap space.

The logic for `sbrk` is simple: it takes an increment value. Since the kernel's PMM allocates memory in pages, if the current increment plus break doesn't cross a page boundary (determined by comparing address masks), we just increase the break. If it does cross a boundary, we allocate new pages as needed and map them. This means user-mode can actually go a bit past the break boundary, since allocation is done in whole pages — as long as you don't exceed the margin provided by the allocated page (but don't do that!).

```cpp
// SBRK(ebx = increment)
uintptr_t sys_sbrk(interrupt_frame* reg) {
    uintptr_t increment = static_cast<uintptr_t>(reg->ebx);
    PCB* cur_pcb = process_list[cur_process_id];
    uintptr_t old_break = cur_pcb->heap_break;
    uintptr_t new_break = old_break + increment;

    if (new_break < cur_pcb->heap_start) return (uintptr_t)-1;

    if (increment > 0) {
        uintptr_t old_page = (old_break + 0xFFF) & ~0xFFF;
        uintptr_t new_page = (new_break + 0xFFF) & ~0xFFF; // page-aligned
        for (uintptr_t addr = old_page; addr < new_page; addr += 0x1000) {
            if (vmm_get_mapping(addr) == 0) {
                uintptr_t phys = reinterpret_cast<uintptr_t>(pmm_alloc(0x1000));
                vmm_map_page(phys, addr, 0x7); // Present | RW | User
            }
        }
    }

    cur_pcb->heap_break = new_break;
    return old_break;
}
```

Then we can implement the user-space side of the system call:

```cpp
void* sbrk(uintptr_t increment) {
    return (void*)syscall1((uintptr_t)SYSCALL::SBRK, increment);
}
```

### malloc/free

Remember how we implemented `kmalloc`/`kfree` earlier? We can actually migrate the kernel heap code to user mode! We just need to replace `kheap_alloc_pages` with the `sbrk` system call — it's very simple:

```cpp
void heap_expand(block_t last_block, uint32_t need_size) {
    uint32_t alloc_bytes = (need_size + 8 + 4095) & ~4095;
    void* result = sbrk(alloc_bytes);
    if ((int)result == -1) return;

    block_t new_block = last_block + block_size(last_block) / 4 + 2;
    set_block_size(new_block, alloc_bytes - 8);
    block_free_mark(new_block);

    heap_size += alloc_bytes;
    set_epilogue((uint32_t*)heap_head - 1);
}

void kheap_expand(free_block block, uint32_t size) {
    uint32_t alloc_pages = (size + 8 + 4095) / 4096;
    free_block new_block = block + block_size(block) / 4 + 2; // point to epilogue
    kheap_alloc_pages(alloc_pages, 0x3);
    set_block_size(new_block, alloc_pages * 4096 - 8);
    block_free(new_block);
    heap_size += alloc_pages;
    set_epilogue();
}

void heap_init() {
    uint32_t init_bytes = 4096;
    uint32_t* base = (uint32_t*)sbrk(init_bytes);
    if ((int)base == -1) return;

    heap_size = init_bytes;

    set_prologue(base);
    heap_head = base + 1;
    set_block_size(heap_head, init_bytes - 4 * 4);
    block_free_mark(heap_head);
    set_epilogue(base);
}

void kheap_init() {
    heap_size = heap_initial_size;
    kheap_head = reinterpret_cast<free_block>(kheap_alloc_pages(heap_initial_size, 0x3));
    set_prologue();
    ++kheap_head;
    set_block_size(kheap_head, heap_initial_size * 4096 - 4 * 4); // symmetric 4-byte block descriptors at head and tail
    block_free(kheap_head);
    set_epilogue();
    return;
}
```

These two chunks of code aren't just similar — they're practically identical...

Now that we have `malloc`/`free`, let's put them to good use in our shell. Previously, our program was reading data into stack space; now let's use the heap:

```cpp
bool try_exec(const char* cmd, int argc, char* argv[]) {
    char fn[MAX_PATH];
    file_stat fst;

    for (int i = 0; i < 2; ++i) {
        snprintf(fn, sizeof(fn), "%s%s", PATH[i], cmd);

        if (stat(fn, &fst) == -1) continue;

        int fd = open(fn, 1);
        if (fd == -1) continue;

        char* buffer = (char*)malloc(fst.size);
        if (!buffer) {
            close(fd);
            continue;
        }

        int size = read(fd, buffer, fst.size);
        close(fd);

        if (size <= 0) {
            free(buffer);
            continue;
        }

        int child_pid = exec(buffer, size, 1, argc, argv);
        free(buffer);
        int ret = waitpid(child_pid);
        return true;
    }
    return false;
}
```

![image-20260302025118535](../assets/自制操作系统（18）：用户态 malloc, free，解析ELF/image-20260302025118535.png)

`pwd` works, but `ll` crashes...

![image-20260302025238964](../assets/自制操作系统（18）：用户态 malloc, free，解析ELF/image-20260302025238964.png)

Everything over 4096 bytes crashes. After some investigation, we found that since we're still in the flat binary stage, the `.bss` segment isn't properly zeroed, so the global variables tracking the heap contain garbage values. Since we're implementing ELF loading in the next section anyway, we'll let AI generate a temporary workaround — allocating a few extra pages to zero out the `.bss` segment.

### Parsing ELF

We've suffered with flat binary long enough — it's time to migrate to ELF!

#### ELF File Structure

```
ELF File
┌─────────────────────────┐
│      ELF Header         │  ← Main entry point, describes basic file info
├─────────────────────────┤
│   Program Header Table  │  ← Directory for the 【loader】(Segments)
├─────────────────────────┤
│                         │
│   .text  .rodata  .data │  ← Actual data chunks
│   .bss   .symtab  ...   │
│                         │
├─────────────────────────┤
│   Section Header Table  │  ← Directory for the 【linker】(Sections)
└─────────────────────────┘
```

An ELF file consists of four main parts:

- A fixed-size header describing basic ELF information;
- The Program Header Table (called "Segments"), which describes a series of instructions telling the loader how to load content from the data blocks, or providing other information (e.g., dynamic linking related data);
- The actual data blocks;
- The Section Header Table (called "Sections"), which provides a more granular subdivision of the data blocks — it's primarily for the linker, and we don't care about it.

In other words, these two tables are, loosely speaking, different ways of describing the same data blocks (to be precise, the data they describe isn't completely identical — segments have some metadata in their own headers that also need to be loaded, while sections describe content that isn't loaded into memory and doesn't have corresponding load segments).

#### Comparison with Flat Binary

So what more is needed to load ELF compared to loading flat binary?

The original flow: a binary has no extra information beyond the instruction stream — just load the flat binary to some address and start executing from there as the entry point;

ELF: besides parsing the ELF Header to verify it's a valid ELF, we also need to parse the Program Header Table, which describes a series of instructions: where to load data blocks (there may be multiple such loads), possibly zero out some memory, and finally jump to the entry point...

#### Loading ELF

We'll only discuss loading `ET_EXEC` ELFs here — no ASLR (Address Space Layout Randomization), let alone PIC (Position-Independent Code).

##### Decomposing create_user_process

Our `create_user_process` function has grown to over 100 lines — not a good sign. We must decompose it.

The current workflow of this function is: create a new user space for the user process → copy the image to a kernel buffer → switch page tables → construct the code area and map it in the new user space → construct the user stack and map it in the new user space → initialize the PCB (construct kernel stack and iret frame) → add the new process to the ready queue → switch back page tables. Based on this, we extract several functions. We'll create a new wrapper function called `exec`, with the same parameters as the original `create_user_process`.

```cpp
pid_t exec(void* code, uint32_t code_size, uint8_t priority, int argc, char** argv);
```

This way, we can even change a few places that use `create_user_process` first:

```cpp
// EXEC(ebx = code, ecx = code_size, edx = priority, esi = argc, ebp = argv)
int sys_exec(interrupt_frame* reg) {
    void*    code      = reinterpret_cast<void*>(reg->ebx);
    uint32_t code_size = reg->ecx;
    uint8_t  priority  = static_cast<uint8_t>(reg->edx);
    int      argc      = static_cast<int>(reg->esi);
    char**   argv      = reinterpret_cast<char**>(reg->ebp);
    return static_cast<int>(exec(code, code_size, priority, argc, argv)); // Changed to Exec here
}
// kernel_main
...
    char* buffer = (char*)kmalloc(65536);
    int size = v_read(cur_pcb, fd, buffer, 65536);
    printf("Executing shell: %d bytes loaded\n", size);

    pid_t shell_pid = exec(buffer, size, 1, 0, nullptr); // Changed to Exec here
    waitpid(shell_pid);
    while (1) {
        yield();
    }
```

Since the logic in `create_user_process` is quite similar to `exec`, let's rename it first, then gradually extract functions.

```cpp
pid_t exec(void* code, uint32_t code_size, uint8_t priority, int argc, char** argv) {
    uint32_t saved_eflags = spinlock_acquire(&process_list_lock);
    pid_t newpid = get_new_pid();

    if (newpid == 0) {
        spinlock_release(&process_list_lock, saved_eflags);
        return 0;
    }

    void* code_buf = copy_image_to_kernel_buffer(code, code_size);

    uint32_t* arg_lens;
    char** arg_bufs;
    copy_args_to_kernel_buffer(argc, argv, arg_lens, arg_bufs);

    uint32_t pd_addr_old = vmm_get_cr3();
    uint32_t pd_addr = vmm_create_page_directory();
    asm volatile ("cli");
    vmm_switch(pd_addr);

    copy_image_from_kernel_buffer(code_buf, code_size);
    
    uintptr_t sp = create_user_stack(USER_STACK_PAGE_SIZE);
    construct_args_for_user_stack(argc, arg_lens, arg_bufs, sp);

    PCB* new_pcb = init_pcb(newpid);
    prepare_pcb_for_new_process(new_pcb);
    new_pcb->cr3 = pd_addr;
    // heap_start also needs to account for .bss extra pages
    uint32_t total_pages = calc_total_pages(code_size);
    new_pcb->heap_start = CODE_SPACE_ADDR + total_pages * 4096;
    new_pcb->heap_break = new_pcb->heap_start;
    init_kernel_stack(new_pcb, KERNEL_STACK_SIZE, sp, CODE_SPACE_ADDR);

    insert_into_scheduling_queue(newpid, priority);

    vmm_switch(pd_addr_old);

    spinlock_release(&process_list_lock, saved_eflags);
    asm volatile ("sti");
    return newpid;
}
```

##### Parsing ELF

After refactoring `create_user_process`, we can now replace the old flat binary logic in the `exec` function.

Our old logic directly copied the flat binary to `CODE_SPACE_ADDR`, padded a few pages of zeros after it, then built the stack after that;

The logic for parsing ELF is: we remove the `CODE_SPACE_ADDR` constant, read the Segment entries from the ELF file copied to the kernel buffer, and when we encounter a `PT_LOAD` directive, we copy the content from the data block to user space as instructed, and map virtual addresses accordingly. So the modified `exec` function becomes:

```cpp
pid_t exec(void* code, uint32_t code_size, uint8_t priority, int argc, char** argv) {
    uint32_t saved_eflags = spinlock_acquire(&process_list_lock);
    pid_t newpid = get_new_pid();

    if (newpid == 0) {
        spinlock_release(&process_list_lock, saved_eflags);
        return 0;
    }

    if (!verify_elf(code, code_size)) {
        return 0;
    }
```

First, there's a validation of the ELF image — if invalid, exit immediately;

```cpp
    if (verify_elf(code, code_size) != 0) {
        return 0;
    }

    // We'll parse the ELF directly in the kernel buffer, no need to copy
    // void* code_buf = copy_image_to_kernel_buffer(code, code_size);

    uint32_t* arg_lens;
    char** arg_bufs;
    copy_args_to_kernel_buffer(argc, argv, arg_lens, arg_bufs);

    uint32_t pd_addr_old = vmm_get_cr3();
    uint32_t pd_addr = vmm_create_page_directory();
    asm volatile ("cli");
    vmm_switch(pd_addr);

    // Not copying, but parsing
    // copy_image_from_kernel_buffer(code_buf, code_size);
    uint32_t entry = 0;
    uint32_t heap_addr = 0;
    if (!construct_user_space_by_elf_image(code, code_size, entry, heap_addr)) {
        vmm_switch(pd_addr_old);
        asm volatile ("sti");
        spinlock_release(&process_list_lock, saved_eflags);
        // todo:
        // dispose_user_space(pd_addr);
        return 0;
    }
```

After switching to the new process's user space, we parse the ELF directly in the kernel buffer to construct the user space. This parsing returns an entry point and a value used to initialize the process's heap space.

For the parsed ELF, the heap starting address is set as follows: record the maximum actual address occupied by `PT_LOAD` segments, page-align it, and set it as this value.

If parsing fails here, besides returning, we also need to destroy the user space — we'll note this as a TODO for now. (We'll fix it later!)

```cpp
    uintptr_t sp = create_user_stack(USER_STACK_PAGE_SIZE);
    construct_args_for_user_stack(argc, arg_lens, arg_bufs, sp);

    PCB* new_pcb = init_pcb(newpid);
    prepare_pcb_for_new_process(new_pcb);
    new_pcb->cr3 = pd_addr;
    // heap_start also needs to account for .bss extra pages
    // uint32_t total_pages = calc_total_pages(code_size);
    // new_pcb->heap_start = entry + total_pages * 4096;
    // new_pcb->heap_break = new_pcb->heap_start;
    new_pcb->heap_start = heap_addr;
    new_pcb->heap_break = heap_addr;
    init_kernel_stack(new_pcb, KERNEL_STACK_SIZE, sp, entry);

    insert_into_scheduling_queue(newpid, priority);

    vmm_switch(pd_addr_old);

    spinlock_release(&process_list_lock, saved_eflags);
    asm volatile ("sti");
    return newpid;
}
```

There are two changes here: first, with a specified entry point, we no longer hardcode the user space starting address; second, we can now safely assign the heap address given by the ELF parser directly to the PCB's corresponding fields.

The remaining work is implementing the two functions `verify_elf` and `construct_user_space_by_elf_image`.

##### Verifying ELF: verify_elf

`verify_elf` checks the magic number, whether it's an ELF we can read, and boundary conditions.

```cpp
int verify_elf(void* elf_image, uint32_t size) {
    if (!elf_image || size < sizeof(Elf32_Ehdr))
        return 0;

    Elf32_Ehdr* ehdr = (Elf32_Ehdr*)elf_image;

    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3)
        return 0;

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32)
        return 0;

    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
        return 0;

    if (ehdr->e_ident[EI_VERSION] != EV_CURRENT)
        return 0;

    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_REL) // no relocation support
        return 0;

    if (ehdr->e_machine != EM_386) // x86
        return 0;

    if (ehdr->e_ehsize != sizeof(Elf32_Ehdr)) // ELF Header size
        return 0;

    if (ehdr->e_phoff + ehdr->e_phnum * sizeof(Elf32_Phdr) > size) // bounds check
        return 0;

    return 1;
}
```

We don't care about sections, so we didn't check section boundaries.

##### Loading ELF into User Space

The steps for loading ELF into user space are:

1. According to the ELF file header, find the start and boundaries of the segment table;
2. Iterate through the segment data using Program Headers;
3. When we encounter a segment of type `PT_LOAD`, copy the corresponding block from the data area to the specified address as instructed;
4. Continue until all segments have been processed.

```cpp
#define VMM_PAGE_PRESENT  (1 << 0)   /* P: bit 0, 1 means page is in memory */
#define VMM_PAGE_WRITABLE (1 << 1)   /* R/W: bit 1, 1 means readable/writable, 0 means read-only */
#define VMM_PAGE_USER     (1 << 2)   /* U/S: bit 2, 1 means user-mode accessible, 0 means kernel only */

uint32_t elf_to_vmm_flags(uint32_t p_flags) {
    uint32_t vmm_flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;
    if (p_flags & PF_W) vmm_flags |= VMM_PAGE_WRITABLE;
    return vmm_flags;
}

int construct_user_space_by_elf_image(void* elf_image, uint32_t size, uint32_t& entry, uint32_t &heap_addr) {
    Elf32_Ehdr* ehdr = (Elf32_Ehdr*)elf_image;
    // We always assume verify_elf was called before construct_user_space_by_elf_image

    // According to the ELF file header, find the start and boundaries of the segment table
    Elf32_Phdr* phdr = (Elf32_Phdr*)((uint32_t)elf_image + ehdr->e_phoff);
    uint16_t phnum = ehdr->e_phnum;
    heap_addr = 0;
    // Iterate through segment data using Program Headers
    for (uint32_t i = 0; i < phnum; ++i) {
        phdr = (Elf32_Phdr*)((uint32_t)elf_image + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type != PT_LOAD) continue;
        if (phdr->p_offset + phdr->p_filesz > size) {
            // User-mode needs to reclaim memory; not implemented yet, mark as todo
            return 0;
        }
        // When we encounter PT_LOAD, copy the corresponding block from data to the specified address
        // Calculate the actual load size needed, page-aligned
        // Mapping pages requires both physical and virtual addresses to be page-aligned
        // Our physical address doesn't matter, but the virtual address from ELF may not be page-aligned!
        // So we need to determine the start and end of each block's virtual address, page-aligned
        uintptr_t aligned_load_vaddr = phdr->p_vaddr & ~0xFFF;
        uintptr_t aligned_load_vaddr_end = (phdr->p_vaddr + phdr->p_memsz + 0xFFF) & ~0xFFF;
        
        uint32_t load_size = aligned_load_vaddr_end - aligned_load_vaddr; 
        void* load_paddr = elf_malloc(load_size);
        uint32_t load_flag = elf_to_vmm_flags(phdr->p_flags);

        for (uintptr_t offset = 0; offset < load_size / 0x1000; ++offset) {
            elf_mmap((uintptr_t)load_paddr + offset * 0x1000, aligned_load_vaddr + offset * 0x1000, load_flag);
        }
            
        memcpy((void*)phdr->p_vaddr, (void*)((uint32_t)elf_image + phdr->p_offset), phdr->p_filesz);

        // If file size and memory size don't match, it's .bss — need to zero manually
        if (phdr->p_filesz < phdr->p_memsz) {
            memset((void*)((uint32_t)phdr->p_vaddr + phdr->p_filesz), 0, phdr->p_memsz - phdr->p_filesz);
        }
        heap_addr = heap_addr < aligned_load_vaddr_end ? aligned_load_vaddr_end : heap_addr;
    }
    entry = ehdr->e_entry;
    return heap_addr > 0 ? 1 : 0;
}
```

![image-20260302222110786](../assets/自制操作系统（18）：用户态 malloc, free，解析ELF/image-20260302222110786.png)

The shell opens fine, but crashes as soon as we execute a program. Why?

```cpp
pid_t exec(void* code, uint32_t code_size, uint8_t priority, int argc, char** argv) {
    uint32_t saved_eflags = spinlock_acquire(&process_list_lock);
    pid_t newpid = get_new_pid();

    if (newpid == 0) {
        spinlock_release(&process_list_lock, saved_eflags);
        return 0;
    }

    if (!verify_elf(code, code_size)) {
        return 0;
    }

    // We'll parse the ELF directly in the kernel buffer, no need to copy
    // void* code_buf = copy_image_to_kernel_buffer(code, code_size);

```

The problem is here — "no need to copy from kernel to user space" doesn't mean "no need to copy to kernel at all"!

```cpp
    if (!construct_user_space_by_elf_image(code_buf, code_size, entry, heap_addr)) {
        kfree(code_buf);
        vmm_switch(pd_addr_old);
        asm volatile ("sti");
        spinlock_release(&process_list_lock, saved_eflags);
        return 0;
    }
    kfree(code_buf);
```

![image-20260302223342502](../assets/自制操作系统（18）：用户态 malloc, free，解析ELF/image-20260302223342502.png)

After fixing this bug, our ELF loader works correctly! Our user-mode programs are back!

---

### Summary

After this battle, using our newly implemented `malloc`/`free`, we've finally abandoned the primitive flat binary and embraced ELF!

In the next section, we'll look at IPC (Inter-Process Communication) and implement pipes!
