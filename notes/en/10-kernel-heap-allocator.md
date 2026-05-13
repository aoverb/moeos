## Homemade OS (10): Kernel Heap Allocator

In the previous chapter, we successfully implemented the Virtual Memory Manager and reorganized the framebuffer mappings. However, the memory we can allocate now has a granularity that's too large. A common scenario is that we only need a few bytes of memory, and the rest is wasted, creating excessive internal fragmentation. Therefore, we need to implement a finer-grained allocator for kernel space — the Kernel Heap Allocator. With this allocator, we'll be able to use `kmalloc` and `kfree` to allocate heap memory, just like using user-space `malloc` and `free`.

### Interface

As usual, let's first look at the external interface to understand what it provides.

```cpp
void kheap_init();
void* kmalloc(uint32_t size);
void kfree(void* addr);
```

...Well, there's not much to deduce here. I just wanted to write them out.

### Implementation

For the kernel heap allocator, given our current situation (make it right), we'll go with the classic **linked list + first fit** approach. The entire heap space is treated as a linked list, where the head and tail of each block record the current block's size and allocation status. Initially, the list has only one entry — the heap space we initially allocate.

Each call to `kmalloc` traverses this list to find the first block that can satisfy our request. Once found, it splits the block into two parts: one of exactly the requested size, and the remaining free space. After recording the allocation info and sizes, it returns the former to the user. If no suitable block is found, we request more space from the VMM to expand our kernel heap.

To better consolidate space, each call to `kfree` not only records the deallocation info but also merges adjacent free blocks to manage fragmentation.

The overall strategy seems fairly simple. There are some details to explain, which we'll cover with the code examples below.

#### Basic Structure

| type       | data           | size        | note                    |
| ---------- | -------------- | ----------- | ----------------------- |
| prologue   | CAFEBABE       | 4 Bytes     |                         |
| block_head | _SIZE \| alloc | 4 Bytes     |                         |
| block_data | …              | _SIZE Bytes |                         |
| block_tail | _SIZE \| alloc | 4 Bytes     | identical to block_head |
| epilogue   | CAFEBABE       | 4 Bytes     |                         |

After initialization, our heap space should have this structure. Let me explain each part:

##### Prologue

The "prologue" header is a 4-byte magic number. When traversing the entire heap space, we can detect this magic number to determine if we've exceeded the heap boundary.

By the way, using the term "prologue" to describe the upper boundary of the heap space is a convention, and it does sound pretty cool.

##### Block

A block is a variable-length structure within the heap space that describes available space. It consists of `block_head`, `block_data`, and `block_tail`.

- **block_head**: 4 bytes, describes the size of `block_data`. Since our `block_data` is 8-byte aligned, the last three bits of its size are always zero, allowing us to hide an allocation flag (alloc) within them.
- **block_data**: The actual data content. The kernel heap allocator doesn't touch this data; it only calculates its size.
- **block_tail**: Identical content to `block_head`. This allows subsequent blocks to find the boundary of the preceding block (without this data, the following block can't know how large the previous block is, and thus can't determine the boundary).

##### Epilogue

The "epilogue" block, like the prologue, is a 4-byte magic number serving the same purpose.

After `kmalloc` or `kfree` operations, the heap layout is simply a few more blocks chained together, which is essentially the same as described above.

#### kheap_init

Let's look at how to initialize the heap space into the layout described above.

Initially, our heap space... doesn't have any space at all. So we need to request an initial contiguous space of `heap_initial_size` (in units of 4KB) from the VMM, using the `vmm_alloc_pages` function we implemented in the previous chapter.

Then we use a pointer to track it. Since our layout has many 4-byte fields, we can use a `uint32_t*` to hold the allocated space:

```cpp
void kheap_init() {
    heap_size = heap_initial_size;
    kheap_head = reinterpret_cast<uint32_t*>(vmm_alloc_pages(heap_initial_size, 0x3));
    
    set_prologue();
    ++kheap_head;
    set_block_size(kheap_head, heap_initial_size * 4096 - 4 * 4); // Two symmetric 4-byte block descriptors at head and tail, recording block size and allocation status
    block_free(kheap_head);
    set_epilogue();
    return;
}
```

Next, we set up the prologue and advance `kheap_head` by four bytes as the actual start of our managed heap space.

Initially, our heap space has only one block, and the actual usable size is `heap_initial_size` minus the four 4-byte descriptor blocks (prologue, epilogue, and the block's own head and tail). We define a `set_block_size` to adjust its size information.

After adjusting, we set the block's allocatable bit to free, set the epilogue, and initialization is complete.

The helper functions involved are just address arithmetic and bit manipulation — they're simple enough that I won't elaborate on them.

#### kmalloc

Now that we've initialized the heap space layout (linked list), when `kmalloc` is called, we use the first-fit strategy to find a block that meets our requirements. We split it into a block of exactly the requested size (rounded up to 8-byte alignment), and the remaining free space:

```cpp
void* kmalloc(uint32_t size) {
    size = (size + 0x7) & ~0x7;
    free_block cur_block = kheap_head;
    while (cur_block) {
        if (!is_block_alloc(cur_block) && block_size(cur_block) >= size) {
            split_block(cur_block, size);
            block_alloc(cur_block);
            return cur_block + 1;
        }
        if (next_block(cur_block)) {
            cur_block = next_block(cur_block);
        } else {
            break;
        }
    }
    kheap_expand(cur_block, size);
    free_block new_block = coalesce(next_block(cur_block));
    split_block(new_block, size);
    block_alloc(new_block);
    return new_block + 1;
}
```

After recording allocation info and sizes, we return the former to the user. If no suitable block is found, we request more space from the VMM to expand our kernel heap.

##### split_block

```cpp
void split_block(free_block block, uint32_t size) {
    if (block_size(block) - size < 0x10) return; // The new block must fit at least 8 bytes for head/tail records + 8 bytes of free space
    uint32_t new_block_size = block_size(block) - size - 0x8;
    set_block_size(block, size);
    free_block new_block = block + size / 4 + 2;
    set_block_size(new_block, new_block_size);
    block_free(new_block);
}
```

When splitting a block, we create a new block that needs an additional 8 bytes of space (the old block's tail and the new block's head). Also, there must be at least 8 bytes of space for the split to be meaningful.

After splitting, we update the respective sizes and mark the new block as available.

##### kheap_expand

```cpp
void kheap_expand(free_block block, uint32_t size) {
    uint32_t alloc_pages = (size + 8 + 4095) / 4096;
    free_block new_block = block + block_size(block) / 4 + 2; // Points to epilogue
    vmm_alloc_pages_at(new_block + 1, alloc_pages, 0x3);
    set_block_size(new_block, alloc_pages * 4096 - 8);
    block_free(new_block);
    heap_size += alloc_pages;
    set_epilogue();
}
```

If the current heap space isn't enough, we calculate the minimum number of pages needed to satisfy the request. But when calculating, we must remember the extra 8 bytes needed to store the new block's information.

Then, we reuse the old epilogue as the new block's header, record it, allocate new pages at the virtual address right after the epilogue, set up the new block's information (size, free status), update the total heap size, and set a new epilogue boundary. Done.

...Wait, *allocate new pages at the virtual address right after the epilogue*? We don't seem to have a function that allocates pages starting from a specific virtual address yet. So we need to go back to the VMM and implement this interface...

...But once we call this interface, wouldn't other programs calling `vmm_alloc_pages` later inevitably run into conflicts?

It seems that our `vmm_alloc_pages` isn't suitable for scenarios requiring expanding contiguous virtual address blocks. Let's take a different approach: implement our own `alloc_pages` inside kheap, and carve out a dedicated address range in the kernel segment of the virtual address space exclusively for kheap:

```cpp
uint32_t kheap_addr_space_begin = 0xD1000000;
constexpr uint32_t kheap_addr_space_end = 0xE1000000;
...

uintptr_t kheap_alloc_pages(uint32_t size, uint32_t flag) {
    uintptr_t ret = kheap_addr_space_begin;
    for (uint32_t i = 0; i < size; i++) {
        if (kheap_addr_space_begin >= kheap_addr_space_end) panic("kheap available space exhausted!");
        if (vmm_get_mapping(kheap_addr_space_begin) != 0) panic("oom when kheap_alloc_pages!");
        uintptr_t p_addr = reinterpret_cast<uintptr_t>(pmm_alloc(1 << 12));
        vmm_map_page(p_addr, kheap_addr_space_begin, flag);
        kheap_addr_space_begin += (1 << 12);
    }
    return ret;
}
```

(We rely on `kheap_addr_space_begin` auto-incrementing to ensure contiguity.)

Then we modify `kheap_init` and `kheap_expand`:

```cpp
void kheap_expand(free_block block, uint32_t size) {
    uint32_t alloc_pages = (size + 8 + 4095) / 4096;
    free_block new_block = block + block_size(block) / 4 + 2; // Points to epilogue
    kheap_alloc_pages(alloc_pages, 0x3);
    set_block_size(new_block, alloc_pages * 4096 - 8);
    block_free(new_block);
    heap_size += alloc_pages;
    set_epilogue();
}
```

#### kfree

The implementation of `kfree` is relatively simple: take the address passed by the user, subtract 4 bytes, and change the allocation status to free:

```cpp
void kfree(void* addr) {
    free_block block_addr = reinterpret_cast<free_block>(addr) - 1;
    block_free(block_addr);
    coalesce(block_addr);
}
```

##### coalesce

Simply freeing memory creates a lot of external fragmentation. We need a function that merges adjacent free space before and after a given block:

```cpp
free_block coalesce(free_block block) {
    free_block prev = prev_block(block);
    free_block next = next_block(block);

    bool prev_free = prev && !is_block_alloc(prev);
    bool next_free = next && !is_block_alloc(next);

    if (prev_free && next_free) {
        set_block_size(prev, block_size(prev) + block_size(block) + block_size(next) + 16);
        block_free(prev);
        return prev;
    } else if (prev_free) {
        set_block_size(prev, block_size(prev) + block_size(block) + 8);
        block_free(prev);
        return prev;
    } else if (next_free) {
        set_block_size(block, block_size(block) + block_size(next) + 8);
        block_free(block);
        return block;
    }

    return block;
}
```

### Putting It Into Practice

Let's modify the original `test` command to test our allocator:

```cpp
} else if (strcmp(input, "test") == 0) {
    uint32_t* test_array = reinterpret_cast<uint32_t*>(kmalloc(4096));

    for (uint32_t i = 0; i < 1024; i++) {
        test_array[i] = i;
    }

    for (uint32_t i = 0; i < 1024; i++) {
        printf("%d ", test_array[i]);
    }

    kfree(test_array);
```

![image-20260213194546818](../assets/自制操作系统（10）：内核堆分配器/image-20260213194546818.png)

Looking good.

---

### Summary

In this chapter, we implemented a kernel heap allocator. We can now use `kmalloc` and `kfree` to allocate memory of arbitrary size!

So far, starting from following osdev to complete the Meaty Skeleton, we've implemented a pixel-mode console, configured the IDT and GDT, built a keyboard driver, a PIT driver, and most recently the painful memory management series: VMM, PMM, and the kernel heap allocator. We've accomplished a lot! Next, we'll step into the world of multi-process... stay tuned!
