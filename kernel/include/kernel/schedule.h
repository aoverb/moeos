#ifndef _KERNEL_SCHEDULE_H
#define _KERNEL_SCHEDULE_H

#include <stdint.h>
#include <kernel/process.h>
#ifdef __cplusplus
extern "C" {
#endif

constexpr uint8_t NUM_PRIORITY = 5;
constexpr uint8_t MAX_PRIORITY = NUM_PRIORITY - 1;

void init_scheduler();

void insert_into_scheduling_queue(pid_t pid, uint8_t priority = MAX_PRIORITY, bool set_quota = true);
void remove_from_scheduling_queue(pid_t pid);

void schedule();
void yield();

#ifdef __cplusplus
}
#endif

#endif