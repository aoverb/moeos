#include <kernel/syscall.h>
#include <syscall_def.h>
#include <kernel/process.h>
#include <kernel/tty.h>
#include <string.h>

constexpr uint32_t MAX_SYSCALL = 255;
syscall_handler_t syscall_table[255] = { nullptr };

uint32_t sys_exit(interrupt_frame*) {
    exit_process(cur_process_id);
    return 0;
}

uint32_t sys_terminal_write(interrupt_frame* reg) {
    terminal_write((const char*)reg->ebx , strlen((const char*)reg->ebx));
    return 0;
}

void syscall_init() {
    register_syscall(uint32_t(SYSCALL::EXIT), sys_exit);
    register_syscall(uint32_t(SYSCALL::TERMINAL_WRITE), sys_terminal_write);
}

void register_syscall(uint8_t n, syscall_handler_t handler) {
    syscall_table[n] = handler;
}

void inner_syscall_handler(interrupt_frame* reg) {
    uint32_t syscall_num = reg->eax;
    if (syscall_num >= MAX_SYSCALL || !syscall_table[syscall_num]) {
        reg->eax = (uint32_t)(SYSCALL_RET::SYSCALL_NOT_FOUND);
        return;
    }
    reg->eax = (syscall_table[syscall_num])(reg);
}