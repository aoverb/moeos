#include <kernel/syscall.h>
#include <syscall_def.h>

constexpr uint32_t MAX_SYSCALL = 255;
syscall_handler_t syscall_table[255] = { nullptr };

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