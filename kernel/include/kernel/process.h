#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
constexpr uint8_t MAX_PROCESSES_NUM = 128;
constexpr uint32_t KERNEL_STACK_SIZE = 4096;

enum class process_state {
    READY = 0,
    RUNNING = 1,
    BLOCKED = 2,
    ZOMBIE = 3
};

typedef struct PCB {
    uint8_t pid;
    uintptr_t esp;
    uintptr_t cr3;
    uint32_t saved_eflags;
    // 该任务的内核栈底（用于释放内存）
    void* kernel_stack_bottom;
    
    uint16_t priority;
    uint16_t quota;
    uint32_t create_time;
    process_state state;
    PCB* prev = nullptr;
    PCB* next = nullptr;
} PCB;

extern PCB* process_list[MAX_PROCESSES_NUM];
extern uint8_t cur_process_id;

typedef PCB* process_queue;
bool insert_into_process_queue(process_queue& queue, PCB* process);
void remove_from_process_queue(process_queue& queue, uint8_t pid);

void process_init();

void print_process();
uint32_t create_process(void* entry, void* args);
uint32_t exit_process(uint8_t pid);

uint32_t create_user_process(void* code, uint32_t code_size, uint8_t priority);

void process_switch_to(uint8_t pid);

void insert_into_process_recycle_queue(PCB* process);
void do_process_recycle();

#ifdef __cplusplus
}
#endif

#endif