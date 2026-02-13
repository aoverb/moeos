#include <kernel/mm.h>
#include <kernel/panic.h>
constexpr uint32_t heap_initial_size = 1;
constexpr uint32_t kheap_magic = 0xCAFEBABE;
uint32_t heap_size;
using free_block = uint32_t*;

free_block kheap_head;

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

void kheap_expand(uint32_t size) {

}

void kheap_init() {
    heap_size = heap_initial_size;
    kheap_head = reinterpret_cast<uint32_t*>(vmm_alloc_pages(heap_initial_size, 0x3));
    set_prologue();
    ++kheap_head;
    set_block_size(kheap_head, heap_initial_size * 4096 - 4 * 4); // 头尾两个4字节的对称的块描述结构，记录块大小和是否已分配的信息
    block_free(kheap_head);
    set_epilogue();
    return;
}

void kmalloc() {

}

void kfree() {

}