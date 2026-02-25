#ifndef _KERNEL_HAL_H
#define _KERNEL_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void hal_init();

uint8_t hal_inb(uint16_t port);
void hal_outb(uint16_t port, uint8_t val);

void hal_enable_irq(uint8_t irq);
void hal_disable_irq(uint8_t irq);

void update_kernel_stack(uint32_t esp);

#ifdef __cplusplus
}
#endif

#endif