#ifndef _ARCH_I386_PIC_H
#define _ARCH_I386_PIC_H

#include <kernel/io.h>

#ifdef __cplusplus
extern "C" {
#endif

void pic_init();
void pic_enable_irq(uint8_t irq);
void pic_disable_irq(uint8_t irq);

#ifdef __cplusplus
}
#endif

#endif