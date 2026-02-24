#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
constexpr uint8_t MAX_PROCESSES_NUM = 128;

typedef struct PCB {
    uint8_t pid;
    uintptr_t esp;
    // 该任务的内核栈底（用于释放内存）
    void* kernel_stack_bottom;

    uint16_t priority;
    uint16_t quota;
    PCB* prev;
    PCB* next;
} PCB;

extern PCB* process_list[MAX_PROCESSES_NUM];
extern uint8_t cur_process_id;

void process_init();

uint32_t create_process(void* entry);
uint32_t exit_process(uint8_t pid);

void process_switch_to(uint8_t pid);

#ifdef __cplusplus
}
#endif

#endif