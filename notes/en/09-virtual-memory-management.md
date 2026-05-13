## Homemade OS (9): Virtual Memory Management

In the previous section, we implemented parsing the free physical memory layout using the Multiboot protocol, and managed these free memory regions with our own Physical Memory Manager (PMM). However, there's still a problem: the memory addresses we get from the PMM are all physical addresses, but we've already enabled paging. This presents us with two issues:

1. Not all free physical memory addresses have corresponding page mappings. That is, a large portion of physical memory cannot be accessed via virtual addresses.

2. Even for physical memory that does have corresponding page mappings, we can't access them directly — we need to map the physical address to a virtual address to use that free memory. Currently, we're doing this with hardcoded offsets (+0xC0000000).

So next, we'll implement Virtual Memory Management to solve both of these problems.

### Basic Interface

Let's first define the core VMM interface. Looking at the problems above, there are really just two key interfaces:

```cpp
// Map virtual address vaddr to physical address paddr, flags are page table entry attributes
void vmm_map(uintptr_t vaddr, uintptr_t paddr, uint32_t flags);

// Unmap the virtual address vaddr
void vmm_unmap(uintptr_t vaddr);
```

### Initial Implementation

The functionality needed by these interfaces is actually quite simple: for a given virtual-to-physical address mapping, we access the corresponding page directory entry and page table entry, configure the descriptors, and write them. But how do we access the page directory and page table? Oh right, when we set up the higher-half kernel earlier, we identity-mapped the 8MB of space starting at 0x0 and 0xC0000000. So when we need to write PTEs and PDEs, we could just use those physical addresses in the mapped range, right? Wrong — this would mean we have two separate systems for managing physical addresses (plus the PMM we already wrote)! And it would make those 8MB of physical addresses a special case that requires special treatment (just like now). So we must break free from this inconsistency and special-casing.

#### The Chicken-and-Egg Problem

So what should we do? If we accept this special treatment, then when we need to access physical addresses beyond the 8MB range, we'd use our PMM to allocate a physical page for the page table. That physical page could be anywhere, so we'd need to construct a virtual address for it. If the physical page happens to fall within our 8MB range, it's fine (since there's a corresponding mapping in the page table). But if it's outside the 8MB range, we'd need to allocate another physical page to construct a virtual address... infinite loop.

How do we break this deadlock? On x86, there's a technique called **"Recursive Mapping"** that solves this problem.

#### Recursive Mapping

Let's first review how the MMU translates our virtual addresses to physical addresses.

On a 32-bit system, the MMU splits a 32-bit virtual address into three parts: the top 10 bits for the PDE index, bits 10-20 for the PTE index, and bits 21-32 for the offset.

The CR3 register holds the physical address of the page directory. The MMU uses the PDE index to find the corresponding page directory entry, which is a 32-bit descriptor containing the physical address of a page table. Following that physical address, the MMU uses the PTE index to find the corresponding page's physical address. A page is 4KB, which matches the offset range.

With recursive mapping, we make one entry in the page directory point to itself. By constructing a special virtual address, we can eventually map to the page directory's own physical address. For example, if we set entry 1023 of the page directory to point to the page directory's physical address, we can access the page directory by visiting virtual address `0xFFFFF000`. Why? Because both the PDE and PTE indices of this virtual address are 1023. The MMU finds the page directory's physical address through PDE[1023], interprets the memory there as a page table, then uses PTE index 1023 to come back to the same place — except this time, we interpret this area as a page, allowing us to freely access it using the offset.

With recursive mapping, let's see how we write a virtual-to-physical address mapping into the page directory. First, we split the virtual address into three parts. For the first part (PDE), we need to find the corresponding page directory entry in the page directory, and from there find the virtual address of the page table it points to. How? We use `0xFFC00000 | (PDE << 12)`. With this virtual address, the MMU interprets the data at CR3's address as a page table (using this virtual address's PDE), and we find the entry at the index of our target virtual address's PDE. This gives us the physical address of the page table pointed to by the PDE. To pinpoint a specific page table entry within that page table, we evolve the virtual address to `0xFFC00000 | (PDE << 12) | (PTE << 2)`, and we can write a page table entry at this address.

One more thing: using `0xFFC00000 | (PDE << 12)` to look up a page table that doesn't exist yet will cause problems. So we need to check beforehand — if no page table is set up for this PDE, we need to allocate one with `pmm_alloc`, set it up, and then continue.

The code implementation looks roughly like this:

```cpp
static inline void flush_tlb() {
    asm volatile(
        "mov %%cr3, %%eax\n"
        "mov %%eax, %%cr3\n"
        ::: "eax"
    );
}

static inline void invlpg(uintptr_t addr) {
    asm volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

void vmm_init() {
    PDE* pde_list = reinterpret_cast<PDE*>(page_directory);
    pde_list[1023] = {0};
    pde_list[1023].frame = pd_addr >> 12;
    pde_list[1023].read_write = 1;
    pde_list[1023].present = 1;

    flush_tlb();
}

void vmm_map_page(uintptr_t p_addr, uintptr_t v_addr, uint32_t flag) {
    if (p_addr & 0xFFF) panic("p_addr not aligned!");
    if (v_addr & 0xFFF) panic("v_addr not aligned!");
    uintptr_t pde = v_addr >> 22;
    uintptr_t pte = v_addr >> 12 & 0x3FF;
    PDE* pde_list = reinterpret_cast<PDE*>(pd_vaddr);
    if (!pde_list[pde].present) {
        // pmm allocates a physical page, writes to the corresponding PDE
        uint32_t new_pt = reinterpret_cast<uint32_t>(pmm_alloc(1 << 12));
        if (!new_pt) panic("oom when trying to allocate new page for page table");
        pde_list[pde] = {0};
        pde_list[pde].user_super = (flag >> 2) & 1;
        pde_list[pde].frame = new_pt >> 12;
        pde_list[pde].read_write = 1;
        pde_list[pde].present = 1;
        invlpg(0xFFC00000 | pde << 12);

        PTE* pte_list = reinterpret_cast<PTE*>(0xFFC00000 | pde << 12);
        memset(pte_list, 0, sizeof(PTE) * 1024);
    }
    PTE* cur_pte = reinterpret_cast<PTE*>(0xFFC00000 | pde << 12 | pte << 2);
    if (cur_pte->present) panic("v_addr mapping already exist!");
    *cur_pte = {0};
    
    cur_pte->read_write = (flag >> 1) & 1;
    cur_pte->user_super = (flag >> 2) & 1;
    cur_pte->present = 1;
    cur_pte->frame = p_addr >> 12;

    invlpg(v_addr);
}
```

#### PDE & PTE

```cpp
// Page Table Entry (4KB page)
typedef struct PTE {
    uint32_t present        : 1;  // P
    uint32_t read_write     : 1;  // R/W
    uint32_t user_super     : 1;  // U/S
    uint32_t write_through  : 1;  // PWT
    uint32_t cache_disable  : 1;  // PCD
    uint32_t accessed       : 1;  // A
    uint32_t dirty          : 1;  // D
    uint32_t pat            : 1;  // PAT
    uint32_t global         : 1;  // G
    uint32_t available      : 3;  // AVL (OS use)
    uint32_t frame          : 20; // Physical page frame number
} PTE;

// Page Directory Entry (points to page table)
typedef struct PDE {
    uint32_t present        : 1;  // P
    uint32_t read_write     : 1;  // R/W
    uint32_t user_super     : 1;  // U/S
    uint32_t write_through  : 1;  // PWT
    uint32_t cache_disable  : 1;  // PCD
    uint32_t accessed       : 1;  // A
    uint32_t reserved       : 1;  // 0 (ignored)
    uint32_t page_size      : 1;  // PS (0 = 4KB pages, 1 = 4MB page)
    uint32_t global         : 1;  // G (ignored if PS=0)
    uint32_t available      : 3;  // AVL (OS use)
    uint32_t frame          : 20; // Page table physical frame number
} PDE;
```

Just focus on setting `present`, `read_write`, `user_super`, and `frame`; zero out the rest during initialization.

#### Testing

When the kernel main program starts, we try to allocate a 4KB physical memory page and map it to virtual address `0xBEEF0000`. Then we access this virtual address, sequentially write some data, and print it out to test the correctness of our VMM implementation:

```cpp
    void* m = pmm_alloc(4096);
    vmm_map_page(reinterpret_cast<uintptr_t>(m), 0xBEEF0000, 0x3);
    uint32_t* test_array = reinterpret_cast<uint32_t*>(0xBEEF0000);
    
    for (uint32_t i = 0; i < 1024; i++) {
        test_array[i] = i;
    }

    for (uint32_t i = 0; i < 1024; i++) {
        printf("%d ", test_array[i]);
    }
```

Testing with this code gives the following result:

![image-20260210211751757](../assets/自制操作系统（9）：虚拟内存管理/image-20260210211751757.png)

If we increase the range of the printf loop below, it triggers a Page Fault (PF) interrupt once we go out of bounds:

![image-20260210211908370](../assets/自制操作系统（9）：虚拟内存管理/image-20260210211908370.png)

### Further Implementation

We've implemented initialization and mapping interfaces. Let's implement more interfaces.

#### Unmapping

Since we can map, we should also be able to unmap. Unmapping is relatively simple — just clear the Present (P) bit of the corresponding page. But for robustness, I'm zeroing out the entire page below. Note that we must remember to call `invlpg` to flush the page table entry.

```cpp
void vmm_unmap_page(uintptr_t v_addr) {
    if (v_addr & 0xFFF) panic("v_addr not aligned!");

    uintptr_t pde = v_addr >> 22;
    uintptr_t pte = v_addr >> 12 & 0x3FF;
    PDE* pde_list = reinterpret_cast<PDE*>(pd_vaddr);
    if (!pde_list[pde].present) {
        return;
    }
    PTE* cur_pte = reinterpret_cast<PTE*>(0xFFC00000 | pde << 12 | pte << 2);
    if (!cur_pte->present) return;
    *cur_pte = {0};

    invlpg(v_addr);
}
```

#### Query Mapping

It's worth noting that although we unmapped above, we didn't free the physical page that was mapped to the virtual address. I think the decision of whether to free should be left to the user. So we can provide a function to query the physical address corresponding to a virtual address, allowing the user to call `pmm_free` to release the physical page:

```cpp
uintptr_t vmm_get_mapping(uintptr_t v_addr) {
    if (v_addr & 0xFFF) panic("v_addr not aligned!");

    uintptr_t pde = v_addr >> 22;
    uintptr_t pte = v_addr >> 12 & 0x3FF;
    PDE* pde_list = reinterpret_cast<PDE*>(pd_vaddr);
    if (!pde_list[pde].present) {
        return 0;
    }
    PTE* cur_pte = reinterpret_cast<PTE*>(0xFFC00000 | pde << 12 | pte << 2);
    if (!cur_pte->present) return 0;
    return (cur_pte->frame << 12);
}
```

#### Cleaning Up Low Address Identity Mapping

Earlier in `boot.s`, we set up identity mapping for the low addresses as well. This was to ensure that EIP wouldn't become invalid after enabling paging, and that the MBI address passed by GRUB would still be usable. However, low address space is very precious, so we need to reclaim it after we're done with the MBI:

```cpp
void vmm_cleanup_low_identity_mapping() {
    PDE* pde_list = reinterpret_cast<PDE*>(page_directory);
    for (int i = 0; i < 2; i++) {
        if (pde_list[i].present) {
            pde_list[i] = {0};
        }
    }
    flush_tlb();
}

...
    
    terminal_initialize(mbi);
    vmm_cleanup_low_identity_mapping(); // After this, low identity mapping is gone, mbi is invalid
    mbi = NULL;
```

However, our PMM uses a buddy system, and the structure pointers stored in `all_pages` and `free_area` are all low addresses, so we obviously can't just clear them directly — we need to remap them. We'll handle this part later.

#### Contiguous Memory Allocation

We often need contiguous virtual memory, so we need to provide a function to allocate such memory.

Let's start with a simple implementation, using `0xC0800000` (the higher-half page we set up in `boot.s`) as the beginning of allocatable memory, incrementing towards higher addresses to allocate virtual memory, and updating the start pointer after allocation. The advantage of this approach is simplicity; the disadvantage is that it doesn't support memory reclamation. But for our current kernel development, this is sufficient. We can consider using an AVL tree for improvement later if needed:

```cpp
continuous_addr_begin = 0xC0800000;
...
uintptr_t vmm_alloc_pages(uint32_t size, uint32_t flag) {
    uintptr_t ret = continuous_addr_begin;
    for (uint32_t i = 0; i < size; i++) {
        if (vmm_get_mapping(continuous_addr_begin) != 0) panic("oom when vmm_alloc_pages!");
        uintptr_t p_addr = reinterpret_cast<uintptr_t>(pmm_alloc(1 << 12));
        vmm_map_page(p_addr, continuous_addr_begin, flag);
        continuous_addr_begin += (1 << 12);
    }
    return ret;
}
```

### Putting It Into Practice

It's about time we took our VMM for a spin.

#### Fixing the Framebuffer Mapping

Remember the pitfall we left earlier? We used large pages to create virtual address mappings for the framebuffer region. It's time to get rid of those ugly temporary solutions.

```cpp
void map_lfb_hardcore(uint32_t phys_addr, uint32_t size) {
    phys_addr &= ~((1 << 12) - 1);
    uintptr_t vram_addr_begin = 0xD0000000;
    uintptr_t num_pages = (size + 0xFFF) / 0x1000;
    
    for (uintptr_t i = 0; i < num_pages; ++i)
        vmm_map_page(phys_addr + i * (1 << 12), vram_addr_begin + i * (1 << 12), 0x3);
}
```

When I thought everything was good to go and booted up, the system crashed immediately. Below is a screenshot I managed to grab before the crash:

![image-20260211194254916](../assets/自制操作系统（9）：虚拟内存管理/image-20260211194254916.png)

We can see that one line of text got split into multiple lines — the column count is wrong — and it crashes on the second `printf`. After investigating with Claude Code, I found:

1. My framebuffer height calculation logic was wrong from the start;
2. My scrolling logic had issues (no wonder it was so slow all this time!);
3. Most critically, I discovered that if I run `pmm_prepare` before `terminal_initialize`, the width becomes 256. Reversing the order fixes it. This is because my `pmm_prepare` didn't account for the MBI, so its contents got corrupted, causing the TTY to get incorrect data.

The earlier issues were minor, but for the `pmm_prepare` issue — I had implemented some very complex nested logic. I had the AI rewrite it directly (this part was really tedious... if I had to write it myself, it would be very hard to move this project forward...). Let myself off the hook on this one.

![image-20260211202648497](../assets/自制操作系统（9）：虚拟内存管理/image-20260211202648497.png)

After this ordeal, although our OS looks the same visually (though scrolling becoming fast is indeed a change!), the internal code implementation has become much cleaner.

### Summary

This time, we implemented virtual memory management. We now have the ability to map any physical address to any virtual address! And using this capability, we've elegantly re-implemented the memory mappings for our kernel! A cause for celebration.

However, you might notice there are still some issues with the VMM. For example, we can only map 4KB at a time — mapping larger spaces requires loops. Or, if we need a contiguous range of virtual addresses, handling it once or twice is fine, but when there are many such requests, managing non-conflicting virtual address mappings becomes a headache. We'll address these issues in the next chapter when we implement the kernel heap allocator.
