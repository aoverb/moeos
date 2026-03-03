#include <driver/pit.h>
#include <kernel/hal.h>
#include <kernel/isr.h>
#include <kernel/schedule.hpp>

#include <stdio.h>

static volatile uint32_t ticks;

void timer_interrupt_handler(registers* /* regs */) {
    ++ticks;
    hal_outb(0x20, 0x20); // EOI
    schedule();
    return;
}

void pit_init() {
    asm volatile ("cli");
    ticks = 0;
    register_interrupt_handler(32, timer_interrupt_handler);
    hal_outb(0x43, 0x34);
    hal_outb(0x40, 0x9c);
    hal_outb(0x40, 0x2e);

    hal_enable_irq(0);
}

void pit_sleep(uint32_t ms) {
    uint32_t saved_ticks = ticks;
    uint32_t ceiling_10ms = (ms + 9) / 10;
    while (ticks - saved_ticks < ceiling_10ms) {
        asm volatile ("hlt");
    }
}

uint32_t pit_get_ticks() {
    return ticks;
}