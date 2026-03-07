#include <driver/pit.h>
#include <kernel/hal.h>
#include <kernel/isr.h>
#include <kernel/schedule.hpp>

#include <stdio.h>

static volatile uint32_t ticks;

void timer_handler(uint32_t current_tick);

void timer_interrupt_handler(registers* /* regs */) {
    ++ticks;
    hal_outb(0x20, 0x20); // EOI
    timer_handler(ticks);
    schedule();
    return;
}

void pit_init() {
    printf("pit initializing...");
    asm volatile ("cli");
    ticks = 0;
    register_interrupt_handler(32, timer_interrupt_handler);
    hal_outb(0x43, 0x34);
    hal_outb(0x40, 0x9c);
    hal_outb(0x40, 0x2e);

    hal_enable_irq(0);
    printf("OK\n");
}

void pit_sleep(uint32_t ms) {
    uint32_t saved_ticks = ticks;
    uint32_t ceiling_10ms = (ms + 9) / 10;
    while (ticks - saved_ticks < ceiling_10ms) {
        asm volatile ("pause");
    }
}

uint32_t pit_get_ticks() {
    return ticks;
}