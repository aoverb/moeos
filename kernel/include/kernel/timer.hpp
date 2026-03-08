#ifndef _KERNEL_TIMER_H
#define _KERNEL_TIMER_H

#include <stdint.h>
#include <driver/pit.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef void (*timer_callback_func)(void* arg);

typedef struct  {
    uint32_t wake_tick;
    timer_callback_func callback_func;
    void* args;
    bool cancelled;
} timer_config;


void init_kernel_timer();
timer_config* register_timer(uint32_t wake_tick, timer_callback_func callback, void* args);
void sleep(uint32_t ms);
void cancel_timer(timer_config* conf);


#ifdef __cplusplus
}
#endif

#endif