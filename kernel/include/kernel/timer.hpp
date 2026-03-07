#ifndef _KERNEL_TIMER_H
#define _KERNEL_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_kernel_timer();
typedef void (*timer_callback_func)(void* arg);
bool register_timer(uint32_t ms_10, timer_callback_func callback, void* args);
void sleep(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif