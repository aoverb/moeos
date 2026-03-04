#ifndef _KERNEL_MM_H
#define _KERNEL_MM_H
#include <stdint.h>
struct multiboot_mmap_entry
{
  uint32_t size;
  uint64_t addr;
  uint64_t len;
#define MULTIBOOT_MEMORY_AVAILABLE              1
#define MULTIBOOT_MEMORY_RESERVED               2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE       3
#define MULTIBOOT_MEMORY_NVS                    4
#define MULTIBOOT_MEMORY_BADRAM                 5
  uint32_t type;
} __attribute__((packed));
#define MAX_ORDER 12

typedef struct multiboot_mmap_entry multiboot_memory_map_t;

typedef struct pm_entry {
    uint32_t begin, end;
} pm_entry;

typedef struct pm_list {
    pm_entry entries[128];
    uint32_t count;
} pm_list;

typedef struct page_frame page_frame;

struct page_frame {
    page_frame *prev, *next;
    uint8_t order;
    uint8_t allocated;
};

void pmm_init(pm_list* pms);

void* pmm_alloc(uint32_t size);

void pmm_free(void* addr);

void pmm_probe();

void pmm_migrate_to_high();

constexpr uint32_t VMM_PRESENT       = (1 << 0);
constexpr uint32_t VMM_WRITABLE      = (1 << 1);
constexpr uint32_t VMM_USER          = (1 << 2);
constexpr uint32_t VMM_WRITE_THROUGH = (1 << 3);
constexpr uint32_t VMM_CACHE_DISABLE = (1 << 4);

void vmm_init();

void vmm_map_page(uintptr_t p_addr, uintptr_t v_addr, uint32_t flag);

void vmm_unmap_page(uintptr_t v_addr);

uintptr_t vmm_get_mapping(uintptr_t v_addr);

void vmm_cleanup_low_identity_mapping();

uintptr_t vmm_alloc_pages(uint32_t size, uint32_t flag);

static inline uintptr_t vmm_get_cr3() {
    uintptr_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void vmm_switch(uint32_t cr3) {
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

uintptr_t vmm_create_page_directory();

void kheap_init();

void* kmalloc(uint32_t size);

void kfree(void* addr);

#endif