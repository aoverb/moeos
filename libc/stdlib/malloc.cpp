#include <stdlib.h>

#include <syscall_def.hpp>
#include <stdint.h>
#include <stddef.h>
#include <string.h>


#define HEAP_MAGIC   0xCAFEBABE
#define MIN_EXPAND   4096

void* sbrk(uintptr_t increment) {
    return (void*)syscall1((uintptr_t)SYSCALL::SBRK, increment);
}

typedef uint32_t* block_t;

block_t heap_head = NULL;
uint32_t heap_size = 0;

inline block_t prev_block(block_t block) {
    if (*(block - 1) == HEAP_MAGIC) return NULL;
    return block - *(block - 1) / 4 - 2;
}

inline block_t next_block(block_t block) {
    if (*(block + *block / 4 + 2) == HEAP_MAGIC) return NULL;
    return block + *block / 4 + 2;
}

inline void adjust_block_tail(block_t block) {
    uint32_t size = *block & ~1;
    *(block + size / 4 + 1) = *block;
}

inline uint32_t block_size(block_t block) {
    return *block & ~1;
}

inline void set_block_size(block_t block, uint32_t size) {
    *block = size | (*block & 1);
    adjust_block_tail(block);
}

inline int is_block_alloc(block_t block) {
    return *block & 1;
}

inline void block_alloc(block_t block) {
    *block |= 1;
    adjust_block_tail(block);
}

inline void block_free_mark(block_t block) {
    *block &= ~1;
    adjust_block_tail(block);
}

void set_prologue(uint32_t* base) {
    *base = HEAP_MAGIC;
}

static void set_epilogue(uint32_t* base) {
    *(base + heap_size / 4 - 1) = HEAP_MAGIC;
}

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

void split_block(block_t block, uint32_t size) {
    if (block_size(block) < size + 0x10) return;
    uint32_t new_size = block_size(block) - size - 8;
    set_block_size(block, size);
    block_t new_block = block + size / 4 + 2;
    set_block_size(new_block, new_size);
    block_free_mark(new_block);
}

block_t coalesce(block_t block) {
    block_t prev = prev_block(block);
    block_t next = next_block(block);
    int prev_free = prev && !is_block_alloc(prev);
    int next_free = next && !is_block_alloc(next);

    if (prev_free && next_free) {
        set_block_size(prev, block_size(prev) + block_size(block) + block_size(next) + 16);
        block_free_mark(prev);
        return prev;
    } else if (prev_free) {
        set_block_size(prev, block_size(prev) + block_size(block) + 8);
        block_free_mark(prev);
        return prev;
    } else if (next_free) {
        set_block_size(block, block_size(block) + block_size(next) + 8);
        block_free_mark(block);
        return block;
    }
    return block;
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

void* malloc(size_t size) {
    if (size == 0) return NULL;
    if (!heap_head) heap_init();
    if (!heap_head) return NULL;

    size = (size + 0x7) & ~0x7;

    block_t cur = heap_head;
    while (cur) {
        if (!is_block_alloc(cur) && block_size(cur) >= size) {
            split_block(cur, size);
            block_alloc(cur);
            return cur + 1;
        }
        if (next_block(cur)) {
            cur = next_block(cur);
        } else {
            break;
        }
    }

    heap_expand(cur, size);
    block_t nb = next_block(cur);
    if (!nb) return NULL;  // sbrk 失败
    block_t new_block = coalesce(nb);
    split_block(new_block, size);
    block_alloc(new_block);
    return new_block + 1;
}

void free(void* ptr) {
    if (!ptr) return;
    block_t block = (block_t)ptr - 1;
    block_free_mark(block);
    coalesce(block);
}