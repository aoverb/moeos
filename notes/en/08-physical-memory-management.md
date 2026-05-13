## Homemade OS (8): Physical Memory Management

In the previous section, we enabled the timer interrupt, implemented the `sleep()` function, and most importantly, filled in the pit left from before — the Higher-Half Kernel. However, due to our overly "hardcore" framebuffer mapping approach, we've created a new pit. But don't worry — the memory management we're about to implement will be the feature that fills this pit.

Now, let's step into the realm of memory management and talk about how to manage physical memory.

Why do we need memory management? Simply put, while memory isn't as precious per-kilobyte as it used to be, it's still not infinite. Ideally, when the kernel or a user process needs memory, we can find a free chunk in memory and allocate it. When it's no longer needed, we can reclaim it for future use.

Memory management comes with its own challenges. For example, what algorithm should we use to quickly find a free block of memory? How do we handle fragmentation — how to avoid situations where the total free memory is larger than what we need, but it's scattered in non-contiguous chunks causing allocation failure (external fragmentation)? How do we avoid wasting memory by allocating much more than actually needed (internal fragmentation)?

Fortunately, these are evergreen problems in the industry, and many brilliant people have proposed useful data structures and algorithms, many of which have stood the test of time. For our OS, we don't need anything too complex right now. Let's first make it right, and later we can make it nice.

### Standing on Giants' Shoulders — Multiboot's Memory Map

Remember how we used the Multiboot protocol to boot our system? It provides a lot of useful information, including a map recording available physical memory addresses.

![image-20260131164319856](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%888%EF%BC%89%EF%BC%9A%E7%89%A9%E7%90%86%E5%86%85%E5%AD%98%E7%AE%A1%E7%90%86/image-20260131164319856.png)

We can use the following code to get the available memory:

```cpp
multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)info->mmap_addr;

while((uint32_t)mmap < info->mmap_addr + info->mmap_length) {
    if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
        // Available memory
        // Record its start and end
    }
    // mmap->size: according to the spec, this field stores "the size of the remaining part of the entry excluding the size field itself."
    mmap = (multiboot_memory_map_t*)((uint32_t)mmap + mmap->size + sizeof(mmap->size));
}
```

---

#### Note: Known Defect

Remember how we enabled paging in the previous section? Our initial paging only set up a 4MB mapping from 0x0 to physical address 0, and from 0xC0000000 to physical address 0.

The memory map information that GRUB passes to us via the Multiboot protocol is stored at a physical address. **This address is a physical address, and theoretically it could be anywhere in memory.** While it's generally placed below 1MB, there's no guarantee. This is another pit we'll need to fix later. A feasible approach: copy this information to our `.bss` section before enabling paging.

---

Oh right, we don't have the `multiboot_memory_map_t` structure yet. Let's define it and first print it out in `kernel_main`.

![image-20260201141850995](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%888%EF%BC%89%EF%BC%9A%E7%89%A9%E7%90%86%E5%86%85%E5%AD%98%E7%AE%A1%E7%90%86/image-20260201141850995.png)

But the regions marked as "Available" in this map are merely "usable" — they're not necessarily unused. The kernel code, page tables, stack, and the Multiboot structures themselves currently reside in this memory.

I initially planned to account for everything occupying memory and then initialize the PMM with corrected data. But this perfectionism would undoubtedly slow us down, and it's quite tedious. I decided to temporarily exclude the kernel program and framebuffer from the available memory list and hand the rest directly to the PMM.

### Excluding Used Regions from the Memory Map

We need to exclude two regions: the kernel and the framebuffer. Let's start with the kernel.

#### Kernel

To exclude the kernel, we need to know where the kernel is loaded in physical memory — its start and end. The start is naturally at 1MB. As for the end — good news! We already set this up in `linker.ld`:

```ld
    }
    
    _kernel_end = . - 0xC0000000;
}
```

We can reference it in C code like this:

```cpp
extern uint64_t _kernel_end;

...
    
    printf("_kernel_end: %lu \n", &_kernel_end);
```

![image-20260201152927002](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%888%EF%BC%89%EF%BC%9A%E7%89%A9%E7%90%86%E5%86%85%E5%AD%98%E7%AE%A1%E7%90%86/image-20260201152927002.png)

#### Framebuffer

The framebuffer's start and end can be obtained directly from the `mbi`:

```cpp
    uint32_t lfb_begin = mbi->framebuffer_addr;
    uint32_t lfb_end = lfb_begin + mbi->framebuffer_width * mbi->framebuffer_height * (mbi->framebuffer_bpp / 8) - 1;

    printf("_lfb: %x - %x\n", lfb_begin, lfb_end);
```

#### Exclusion

Using the same approach as above, we check each memory segment for overlap. If there's overlap, we remove the overlapping portion, align to 4KB, filter the remaining parts, and store them in a structure array:

```cpp
typedef struct pm_entry {
    uint64_t begin, end;
} pm_entry;

typedef struct pm_list {
    pm_entry entries[128];
    uint32_t count;
} pm_list;
```

Then we pass this array to `pmm_init` to initialize the physical memory allocator.

### Physical Memory Allocator: Buddy System

For our Physical Memory Manager (PMM) implementation, we'll use a buddy system.

#### Data Structure

The buddy system works like this: it divides your available memory into blocks of size 4KB, 8KB, 16KB... up to 2^MAX_ORDER * 4KB. When someone requests a block of memory, the system finds a block just large enough and returns it:

```c
#define MAX_ORDER 12
typedef struct page_frame {
    page_frame *prev, *next;
    size_t order;
    uint8_t allocated;
} page_frame;

page_frame* free_area[MAX_ORDER];
```

Allocated blocks are called "page frames" — distinct from "pages" in the virtual memory context. As you can see above, `free_area` is an array where each member holds a pointer to a `page_frame`, and `page_frame` itself forms a linked list structure. That's essentially the buddy system's data structure: array + linked list.

#### Initialization

During initialization, we should adopt the following strategy: observe the starting address of the current available memory, and provided the address is aligned, cut off the largest possible block, record it in a `page_frame`, and add it to the appropriate order's linked list in `free_area`.

```c
typedef struct pm_entry {
    uintptr_t begin, end;
} pm_entry;
typedef struct pm_list {
    pm_entry entries[128];
    uint32_t count;
} pm_list;

void pmm_init(pm_list* pms) {
    for (int i = 0; i < MAX_ORDER; i++) {
        free_area[i] = NULL;
    }
    for (int i = 0; i < pms->count; i++) {
        uintptr_t begin = pms->entries[i].begin;
        uintptr_t end = pms->entries[i].end;
        while (begin < end) {
            size_t order = (begin == 0) ? MAX_ORDER - 1 : __builtin__ctzll(begin) - 12;
            while ((begin + (1UL << (order + 12))) > end) --order;
            page_frame* pf = (page_frame*)begin;
            if (free_area[order]) {
                free_area[order]->prev = pf;
            }
            pf->order = order;
            pf->allocated = 0;
            pf->prev = NULL;
            pf->next = free_area[order];
            free_area[order] = pf;
            begin += 1UL << (order + 12);
        }
    }
}
```

Why do we need address alignment? Because the buddy system's splitting and merging operations depend on this property. It ensures: 1) Less external fragmentation; 2) Efficient allocation algorithm.

Initialization problem: Look at the following code:

```c
            page_frame* pf = (page_frame*)begin;
            pf->order = order;
            pf->allocated = 0;
            pf->prev = NULL;
            pf->next = free_area[order];
            free_area[order] = pf;
```

We're placing the metadata about the allocated free page frame directly into this free memory. But there's a problem: once this memory is borrowed, the data there is no longer under our control. When the memory is returned, we only have a starting address — we can't know the order. So it's better to store such structures in a contiguous array.

How much space do we need? We're on a 32-bit system, supporting up to 4GB of physical addressing. So we need to record 4GB/4KB = 2^20 page frames, each 16 bytes — that's 16MB of space. Where does this 16MB come from? Perhaps we can also carve it out from this free space:

```c
void pmm_init(pm_list* pms) {
    for (uint8_t i = 0; i < MAX_ORDER; i++) {
        free_area[i] = 0;
    }

    uintptr_t all_end = 0;
    for (uint8_t i = 0; i < pms->count; i++) {
        if (pms->entries[i].end > all_end) all_end = pms->entries[i].end;
    }
    
    all_pages = 0;
    uint8_t flag = 0;
    uintptr_t all_size = all_end + 1;
    for (uint8_t i = 0; i < pms->count; i++) {
        uint32_t cur_size = pms->entries[i].end - pms->entries[i].begin + 1;
        if (cur_size >= sizeof(page_frame) * (all_size / (1 << 12))) {
            flag = 1;
            all_pages = (page_frame*)(pms->entries[i].begin);
            pms->entries[i].begin += sizeof(page_frame) * (all_size / (1 << 12));
            pms->entries[i].begin = (pms->entries[i].begin + 0xFFF) & ~0xFFF;
            break;
        }
    }

    page_limit = (all_size / (1 << 12));
    
    if (!flag) {
        panic("insufficient memory!");
    }
    
    for (uint32_t i = 0; i <= (all_size / (1 << 12)); ++i) {
        all_pages[i].allocated = 1;
    }

    for (uint8_t i = 0; i < pms->count; i++) {
        uintptr_t begin = pms->entries[i].begin;
        uintptr_t end = pms->entries[i].end;
    
        if (end - begin + 1 < (1 << 12)) continue;
        while (begin < end) {
            uint8_t order = (begin == 0) ? MAX_ORDER - 1 : __builtin_ctzll(begin) - 12;
            if (order >= MAX_ORDER) order = MAX_ORDER - 1;
            while ((end - begin + 1 < (1UL << (order + 12)))) --order;
            uintptr_t idx = begin >> 12;
            if (free_area[order]) {
                free_area[order]->prev = &all_pages[idx];
            }
            all_pages[idx].order = order;
            all_pages[idx].allocated = 0;
            all_pages[idx].prev = 0;
            all_pages[idx].next = free_area[order];
            free_area[order] = &all_pages[idx];
            printf("%x order:%d\n", begin, order);
            begin += 1UL << (order + 12);
        }
    }
}
```

And our code became what it is above.

#### Allocation

After initializing our data structure, we can implement the allocation function.

Our allocation function should work like this: the caller passes in the required memory size. Either allocation fails and returns a null pointer, or it succeeds and returns a pointer to the starting address.

The allocation strategy: first, look in `free_area` for a block just large enough to satisfy the user's request. If found, return it directly. If not, look for a larger block. After finding one, split the large block into a chunk just big enough for the user and return it. The remaining parts are split into blocks of various orders according to a greedy strategy and placed back into the respective linked lists.

We can implement this elegantly with a recursive approach:

```c
void* pmm_alloc(uint32_t size) {
    if (size < (1 << 12)) {
        size = 1 << 12;
    } else if ((size & (size - 1)) != 0) {
        size = 1UL << (64 - __builtin_clzll(size - 1));
    }
    uint8_t order = __builtin_ctzll(size) - 12;
    if (order >= MAX_ORDER) return 0;
    if (free_area[order]) {
        void* ret = (void*)((free_area[order] - all_pages) << 12);
        free_area[order]->allocated = 1;
        if (free_area[order]->next) {
            free_area[order]->next->prev = 0;
        }
        free_area[order] = free_area[order]->next;
        return ret;
    } else {
        void* cur_ret = pmm_alloc(1 << (order + 12 + 1));
        if (!cur_ret) return 0;
        all_pages[(uintptr_t)cur_ret >> 12].order = order;
        // Only half is for us; the other half goes back to this order's free_area
        uint32_t free_idx = (uint32_t)((uintptr_t)cur_ret + (1 << (order + 12))) >> 12;
        all_pages[free_idx].prev = 0;
        all_pages[free_idx].next = 0;
        all_pages[free_idx].allocated = 0;
        all_pages[free_idx].order = order;
        free_area[order] = &all_pages[free_idx];
        return cur_ret;
    }
}
```

#### Free

We also need a function to reclaim freed memory. During reclamation, we need to check if there's a similarly sized free block adjacent to it. If so, we merge them cyclically upward into a larger free block.

First, define the interface:

```c
void pmm_free(void* addr);
```

Then let's see what to do. First, we convert the address to an index in our `all_pages` array — just right-shift by 12 bits. After finding the corresponding page frame record, we set `allocated` to 0. But that's not enough. We look for the address that differs only in the bit corresponding to the current order. If it's free, we remove that block from its order's linked list and increase our current order by 1. We repeat until no free buddy is found or the maximum order is reached:

```c
uintptr_t get_buddy_index(uintptr_t idx) {
    return (idx ^ (1 << (all_pages[idx].order)));
}

void pmm_free(void* addr) {
    uintptr_t cur_index = (uintptr_t)addr >> 12;
    page_frame* pf = &all_pages[cur_index];
    pf->allocated = 0;
    uint8_t order = pf->order;

    uint32_t buddy_index = get_buddy_index(cur_index);
    while (order < MAX_ORDER - 1 && buddy_index < page_limit && all_pages[buddy_index].order == order &&
        all_pages[buddy_index].allocated == 0) {
        page_frame* buddy_pf = &all_pages[buddy_index];
        if (buddy_pf->prev) buddy_pf->prev->next = buddy_pf->next;
        else free_area[buddy_pf->order] = buddy_pf->next;
        if (buddy_pf->next) buddy_pf->next->prev = buddy_pf->prev;
        
        cur_index &= buddy_index;
        pf = &all_pages[cur_index];
        ++order;
        pf->order = order;
        buddy_index = get_buddy_index(cur_index);
    }
    pf->prev = 0;
    pf->next = free_area[pf->order];
    if (free_area[pf->order]) free_area[pf->order]->prev = pf;
    free_area[pf->order] = pf;
}
```

Let's run the program. I tested two things: first, after `alloc` then `free`, the `free_area` structure remains unchanged; second, `alloc` can correctly use up all of `free_area` without errors.

By the way, our allocation of `all_pages` is at a physical address. Since we enabled identity mapping starting from address 0 for 8MB, we can access it normally. But if our system's total runtime memory were larger (currently 128MB), allocations at farther physical addresses would cause problems (Page Fault). We'll come back to fix this later.

### Discussion: Another Pit

After switching to the higher-half kernel with paging, I encountered occasional triple faults. Sometimes adding `while(1)` or other code would make the fault appear or disappear. Later, I found it was triggered when writing to the framebuffer. As mentioned before, for framebuffer memory addresses, I used large pages for mapping, and this mapping required writing descriptors to the page directory's corresponding memory location — and the page directory address was hardcoded.

Only later did I realize that the address of `page_directory` had changed, because our linker script was written like this:

```
...
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
...
```

As you can see, as the code grows, the position of the page directory in the `.data` section can change!

This is what triggered the strange, intermittent triple fault issue. It made me appreciate the precision required in low-level programming.

---

After much effort, we've finally implemented the PMM. There were quite a few details, and I learned a lot in the process. This is also the first time we've integrated an algorithm into our operating system.

I originally planned to implement VMM (Virtual Memory Management) in this section as well, but this chapter is already too long (and debugging took forever!), so we'll leave it for the next section.
