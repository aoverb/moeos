#include "idt.h"
#include "io.h"
#include <stdio.h>
#include <string.h>
#include <kernel/process.h>

idt_entry_struct idt_entries[256];
extern "C" void system_call_handler();

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t dpl) {
    idt_entries[num].offset_low  = base & 0xFFFF;
    idt_entries[num].offset_high = (base >> 16) & 0xFFFF;
    idt_entries[num].selector    = sel;
    idt_entries[num].zero        = 0;
    idt_entries[num].gate_type       = 0xE;
    idt_entries[num].storage_segment = 0;
    idt_entries[num].dpl            = dpl;
    idt_entries[num].present        = 1;
}

void inner_interrupt_handler(registers* regs) {
    if ((regs->int_no >= 32 && regs->int_no <= 47)) {
        if (interrupt_handlers[regs->int_no]) {
            (interrupt_handlers[regs->int_no])(regs);
        }
        outb(0x20, 0x20);
        return;
    }
    set_color(0x00FF0000);
    printf("int:  %d\n", regs->int_no);
    printf("An critical error has occurred: %d\n", regs->err_code);
    exit_process(cur_process_id);
    set_color(0x00FFFFFF);
    return;
}

void idt_set_gates() {
    SET_ISR(0);
    SET_ISR(1);
    SET_ISR(2);
    SET_ISR(3);
    SET_ISR(4);
    SET_ISR(5);
    SET_ISR(6);
    SET_ISR(7);
    SET_ISR(8);
    SET_ISR(9);
    SET_ISR(10);
    SET_ISR(11);
    SET_ISR(12);
    SET_ISR(13);
    SET_ISR(14);
    SET_ISR(15);
    SET_ISR(16);
    SET_ISR(17);
    SET_ISR(18);
    SET_ISR(19);
    SET_ISR(20);
    SET_ISR(21);
    SET_ISR(22);
    SET_ISR(23);
    SET_ISR(24);
    SET_ISR(25);
    SET_ISR(26);
    SET_ISR(27);
    SET_ISR(28);
    SET_ISR(29);
    SET_ISR(30);
    SET_ISR(31);
    SET_ISR(32);
    SET_ISR(33);
    SET_ISR(34);
    SET_ISR(35);
    SET_ISR(36);
    SET_ISR(37);
    SET_ISR(38);
    SET_ISR(39);
    SET_ISR(40);
    SET_ISR(41);
    SET_ISR(42);
    SET_ISR(43);
    SET_ISR(44);
    SET_ISR(45);
    SET_ISR(46);
    SET_ISR(47);

    idt_set_gate(0x80, (uint32_t)(&system_call_handler), 0x08, 3);
}

void idt_init() {
    memset(idt_entries, 0, sizeof(idt_entry_struct) * 256);

    idtr_descriptor idtr_sel;
    idtr_sel.base = (uint32_t)(&idt_entries);
    idtr_sel.limit = sizeof(idt_entry_struct) * 256 - 1;

    idt_set_gates();

    asm volatile(
        "lidt %0"
        :
        : "m"(idtr_sel)
        : "memory"
    );
    return;
}