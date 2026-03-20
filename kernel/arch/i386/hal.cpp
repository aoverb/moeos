#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include <kernel/hal.h>

extern "C" int printf(const char* fmt, ...);
void hal_init() {
    printf("HAL initializing...");
    asm volatile ("cli");
    gdt_init();
    pic_init();
    idt_init();
    io_init();
    printf("OK\n");
}

uint8_t hal_inb(uint16_t port) {
    return inb(port);
}

void hal_outb(uint16_t port, uint8_t val) {
    outb(port, val);
}

void hal_enable_irq(uint8_t irq) {
    asm volatile ("cli");
    pic_enable_irq(irq);
}
void hal_disable_irq(uint8_t irq) {
    asm volatile ("cli");
    pic_disable_irq(irq);
}

void update_kernel_stack(uint32_t esp) {
    tss_set_kernel_stack(esp);
}
