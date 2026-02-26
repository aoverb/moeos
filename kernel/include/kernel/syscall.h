#ifndef _ARCH_I386_SYSCALL_H
#define _ARCH_I386_SYSCALL_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t ds;
    uint32_t es;

    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp;
    uint32_t ss;
} interrupt_frame;

void syscall_init();

typedef uint32_t (*syscall_handler_t)(interrupt_frame*);
void inner_syscall_handler(interrupt_frame* reg);
void register_syscall(uint8_t n, syscall_handler_t handler);

#ifdef __cplusplus
}
#endif

#endif