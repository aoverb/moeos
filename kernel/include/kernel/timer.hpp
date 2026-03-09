#ifndef _KERNEL_TIMER_H
#define _KERNEL_TIMER_H

#include <stdint.h>
#include <kernel/process.hpp>
#include <driver/pit.h>

#ifdef __cplusplus
extern "C" {
#endif
using timer_id_t = uint64_t;
typedef void (*timer_callback_func)(pid_t pid, void* arg);

typedef struct  {
    timer_id_t id;
    pid_t pid;
    uint32_t wake_tick;
    timer_callback_func callback_func;
    void* args;
    bool cancelled;
} timer_config;


void init_kernel_timer();
timer_id_t register_timer(uint32_t wake_tick, timer_callback_func callback, void* args);
void sleep(uint32_t ms);
void cancel_timer(timer_config* conf);
void timeout(process_queue* queue, uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif