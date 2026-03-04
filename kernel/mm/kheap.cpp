#include <kernel/mm.hpp>
#include <stdio.h>
#include <kernel/panic.h>
constexpr uint32_t heap_initial_size = 1;
constexpr uint32_t kheap_magic = 0xCAFEBABE;
uint32_t kheap_addr_space_begin = 0xD1000000;
constexpr uint32_t kheap_addr_space_end = 0xE1000000;
uint32_t heap_size;
using free_block = uint32_t*;

free_block kheap_head;

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

inline free_block prev_block(free_block block) {
    if(*(block - 1) == kheap_magic) return nullptr;
    return block - *(block - 1) / 4 - 2;
}
inline free_block next_block(free_block block) {
    if(*(block + *block / 4 + 2) == kheap_magic) return nullptr;
    return block + *block / 4 + 2;
}

inline void adjust_block_tail(free_block block) {
    uint32_t* size = block;
    uint32_t* tail = block + *size / 4 + 1;
    *tail = *size;
}

inline uint32_t block_size(free_block block) {
    // 我们很容易就知道，往块的地址往前偏移四个字节就能获得块信息
    // 最后一位是堆是否分配的标记，我们要掩码掉
    uint32_t* size = block;
    return *size & ~1;
}

inline void set_block_size(free_block block, uint32_t size) {
    if ((size & 0x7) != 0) panic("block_size not aligned");
    uint32_t* ori_size = block;
    *ori_size = size | (*ori_size & 1);
    adjust_block_tail(block);
}

inline uint8_t is_block_alloc(free_block block) {
    return *block & 1;
}

inline void block_alloc(free_block block) {
    uint32_t* ori_size = block;
    *ori_size |= 1;
    adjust_block_tail(block);
}

inline void block_free(free_block block) {
    uint32_t* ori_size = block;
    *ori_size &= ~1;
    adjust_block_tail(block);
}

void set_prologue() {
    uint32_t* prologue = kheap_head;
    *prologue = kheap_magic;
}

void set_epilogue() {
    uint32_t* epilogue = kheap_head + (heap_size * 4096) / 4 - 2;
    *epilogue = kheap_magic;
}

void kheap_expand(free_block block, uint32_t size) {
    uint32_t alloc_pages = (size + 8 + 4095) / 4096;
    free_block new_block = block + block_size(block) / 4 + 2; // 指向epilogue
    kheap_alloc_pages(alloc_pages, 0x3);
    set_block_size(new_block, alloc_pages * 4096 - 8);
    block_free(new_block);
    heap_size += alloc_pages;
    set_epilogue();
}

void kheap_init() {
    heap_size = heap_initial_size;
    kheap_head = reinterpret_cast<free_block>(kheap_alloc_pages(heap_initial_size, 0x3));
    set_prologue();
    ++kheap_head;
    set_block_size(kheap_head, heap_initial_size * 4096 - 4 * 4); // 头尾两个4字节的对称的块描述结构，记录块大小和是否已分配的信息
    block_free(kheap_head);
    set_epilogue();
    return;
}

void split_block(free_block block, uint32_t size) {
    if (block_size(block) < size + 0x10) return; // 新的块应该至少能挤出头尾记录共8字节 + 8字节的空闲空间
    uint32_t new_block_size = block_size(block) - size - 0x8;
    set_block_size(block, size);
    free_block new_block = block + size / 4 + 2;
    set_block_size(new_block, new_block_size);
    block_free(new_block);
}

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

void* kmalloc(uint32_t size) {
    uint32_t flags;
    asm volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    
    size = (size + 0x7) & ~0x7;
    free_block cur_block = kheap_head;
    while (cur_block) {
        if (!is_block_alloc(cur_block) && block_size(cur_block) >= size) {
            split_block(cur_block, size);
            block_alloc(cur_block);
            asm volatile ("pushl %0; popfl" : : "r"(flags) : "memory");
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
    asm volatile ("pushl %0; popfl" : : "r"(flags) : "memory");
    return new_block + 1;
}

void kfree(void* addr) {
    uint32_t flags;
    asm volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    free_block block_addr = reinterpret_cast<free_block>(addr) - 1;
    block_free(block_addr);
    coalesce(block_addr);
    asm volatile ("pushl %0; popfl" : : "r"(flags) : "memory");
}