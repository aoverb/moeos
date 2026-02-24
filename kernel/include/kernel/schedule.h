#ifndef _KERNEL_SCHEDULE_H
#define _KERNEL_SCHEDULE_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void init_scheduler();

void insert_into_scheduling_queue(uint8_t pid);
void remove_from_scheduling_queue(uint8_t pid);

void schedule();
void yield();

#ifdef __cplusplus
}
#endif

#endif