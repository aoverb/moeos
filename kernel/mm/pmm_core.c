#include <kernel/mm.h>
#include <kernel/panic.h>
#include <string.h>

page_frame* all_pages;

page_frame* free_area[MAX_ORDER];

uint32_t page_limit;

void pmm_probe() {
    uint32_t i = 0;
    for(;i < MAX_ORDER; i++) {
        page_frame* pf = free_area[i];
        uint8_t cnt = 0;
        while (pf) {
            // printf("order %d[%d]:%x->", pf->order, pf->allocated, ((uint32_t)(pf - all_pages) << 12));
            pf = pf->next;
            ++cnt;
        }
        // printf(", total: %d\n", cnt);
    }
}

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
            pms->entries[i].begin = (pms->entries[i].begin + 0xFFF) & ~0xFFF; // 确保地址是4KB对齐的
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
            while ((end - begin + 1 < (1UL << (order + 12)))) --order; // 12是指page frame size为4096
            uintptr_t idx = begin >> 12;
            if (free_area[order]) {
                free_area[order]->prev = &all_pages[idx];
            }
            all_pages[idx].order = order;
            all_pages[idx].allocated = 0;
            all_pages[idx].prev = 0;
            all_pages[idx].next = free_area[order];
            free_area[order] = &all_pages[idx];
            begin += 1UL << (order + 12);
        }
    }
}

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
        // 只有一半是我们要用到的，另一半我们加入到本阶的free_area里
        uint32_t free_idx = (uint32_t)((uintptr_t)cur_ret + (1 << (order + 12))) >> 12;
        all_pages[free_idx].prev = 0;
        all_pages[free_idx].next = 0;
        all_pages[free_idx].allocated = 0;
        all_pages[free_idx].order = order;
        free_area[order] = &all_pages[free_idx];
        return cur_ret;
    }
}

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

void pmm_migrate_to_high() {
    uint32_t total_size = page_limit * sizeof(page_frame);

    // 1. 在高地址分配新空间
    page_frame* new_all_pages = (page_frame*)kmalloc(total_size);

    // 2. 拷贝数据
    memcpy(new_all_pages, all_pages, total_size);

    // 3. 计算偏移量，修正指针
    intptr_t delta = (intptr_t)new_all_pages - (intptr_t)all_pages;

    for (uint32_t i = 0; i < page_limit; i++) {
        if (new_all_pages[i].next)
            new_all_pages[i].next = (page_frame*)((uintptr_t)new_all_pages[i].next + delta);
        if (new_all_pages[i].prev)
            new_all_pages[i].prev = (page_frame*)((uintptr_t)new_all_pages[i].prev + delta);
    }

    for (uint32_t i = 0; i < MAX_ORDER; i++) {
        if (free_area[i])
            free_area[i] = (page_frame*)((uintptr_t)free_area[i] + delta);
    }

    // 4. 切换
    page_frame* old_all_pages = all_pages;
    all_pages = new_all_pages;

    // 旧的物理页不回收了，直接忽略
}
