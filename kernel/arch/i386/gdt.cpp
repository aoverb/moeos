#include "gdt.h"
#include <string.h>

static gdt_entry_struct gdt_entries[6];
static tss_entry tss;

void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, 
                  uint8_t dpl, uint8_t executable) 
{
    gdt_entry_struct *entry = &gdt_entries[num];

    entry->base_low    = (base & 0xFFFF);
    entry->base_middle = (base >> 16) & 0xFF;
    entry->base_high   = (base >> 24) & 0xFF;

    entry->limit_low   = (limit & 0xFFFF);
    entry->limit_high  = (limit >> 16) & 0x0F;

    // 设置 Access 字节
    entry->present         = 1;
    entry->dpl             = dpl;
    entry->descriptor_type = 1; // 代码/数据段
    entry->executable      = executable;
    entry->read_write      = 1;
    entry->conforming_expand = 0;
    entry->accessed        = 0;

    // 设置 Flags
    entry->granularity     = 1; // 4KB 粒度
    entry->default_size    = 1; // 32位模式
    entry->long_mode       = 0;
    entry->available       = 0;
}

void gdt_set_tss(int32_t num, uint32_t base, uint32_t limit) {
    gdt_entry_struct *entry = &gdt_entries[num];

    entry->base_low    = (base & 0xFFFF);
    entry->base_middle = (base >> 16) & 0xFF;
    entry->base_high   = (base >> 24) & 0xFF;

    entry->limit_low   = (limit & 0xFFFF);
    entry->limit_high  = (limit >> 16) & 0x0F;

    // Access 字节: present=1, DPL=0, descriptor_type=0 (系统段), type=0x9
    entry->present         = 1;
    entry->dpl             = 0;
    entry->descriptor_type = 0;  // 系统段
    entry->executable      = 1;  // type bit 3 = 1
    entry->conforming_expand = 0;  // type bit 2 = 0
    entry->read_write      = 0;  // type bit 1 = 0
    entry->accessed        = 1;  // type bit 0 = 1
    // type = 1001b = 0x9 = 32-bit available TSS

    // Flags
    entry->granularity     = 0;  // 字节粒度
    entry->default_size    = 0;  // TSS 中此位为 0
    entry->long_mode       = 0;
    entry->available       = 0;
}

void load_gdtr() {
    gdtr_descriptor gdtr_desc;
    gdtr_desc.base = (uint32_t)(&gdt_entries);
    gdtr_desc.limit = sizeof(gdt_entry_struct) * 6 - 1;

    asm volatile("lgdt %0" : : "m"(gdtr_desc));
    
    // 刷新段寄存器
    asm volatile(
        "movw $0x10, %%ax \n\t"
        "movw %%ax, %%ds \n\t"
        "movw %%ax, %%es \n\t"
        "movw %%ax, %%fs \n\t"
        "movw %%ax, %%gs \n\t"
        "movw %%ax, %%ss \n\t"
        "ljmp $0x08, $1f \n\t"  // 远跳转刷新 CS
        "1:"
        : : : "ax"
    );
}

void load_tr() {
    asm volatile(
        "mov $0x28, %ax \n"
        "ltr %ax"
    );
}

void tss_set_kernel_stack(uint32_t esp) {
    tss.esp0 = esp;
}

void tss_init(uint32_t kernel_ss, uint32_t kernel_esp) {
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = kernel_ss;
    tss.esp0 = kernel_esp;
    tss.iomap_base = sizeof(tss_entry);
}

void gdt_init() {
    tss_init(0x10, 0);  // esp0 后续由调度器更新
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFF, 0, 1);
    gdt_set_gate(2, 0, 0xFFFFF, 0, 0);
    gdt_set_gate(3, 0, 0xFFFFF, 3, 1);
    gdt_set_gate(4, 0, 0xFFFFF, 3, 0);
    gdt_set_tss(5, (uint32_t)&tss, sizeof(tss) - 1);
    load_gdtr();
    load_tr();
}