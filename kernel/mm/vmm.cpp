#include <stdint.h>
#include <string.h>
#include <kernel/mm.h>
#include <kernel/panic.h>

extern uint8_t page_directory[];
uintptr_t pd_addr = (uintptr_t)page_directory - 0xC0000000;
constexpr uintptr_t pd_vaddr = 0xFFFFF000;

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

static_assert(sizeof(PTE) == 4);
static_assert(sizeof(PDE) == 4);

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
        // pmm分配一个物理页，写入对应的PDE
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

void vmm_unmap_page(uintptr_t v_addr) {
    if (v_addr & 0xFFF) panic("v_addr not aligned!");

    uintptr_t pde = v_addr >> 22;
    uintptr_t pte = v_addr >> 12 & 0x3FF;
    PDE* pde_list = reinterpret_cast<PDE*>(pd_vaddr);
    if (!pde_list[pde].present) {
        return;
    }
    PTE* cur_pte = reinterpret_cast<PTE*>(0xFFC00000 | pde << 12 | pte << 2);
    if (!cur_pte->present) panic("v_addr mapping already exist!");
    *cur_pte = {0};

    invlpg(v_addr);
}

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