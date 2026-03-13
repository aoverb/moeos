#ifndef _KERNEL_ISR_H
#define _KERNEL_ISR_H

#include <stdint.h>
#include <register.h>

typedef void (*interrupt_handler_t)(registers*);

#ifdef __cplusplus
extern "C" {
#endif

extern interrupt_handler_t interrupt_handlers[256];

void register_interrupt_handler(uint8_t n, interrupt_handler_t handler);

#ifdef __cplusplus
}
#endif

#endif
