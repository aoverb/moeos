#include <kernel/process.h>
#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/schedule.h>
#include <driver/pit.h>
#include <string.h>
#include <stdio.h>

PCB* process_list[MAX_PROCESSES_NUM] = {};
uint8_t cur_process_id = 0;

extern uintptr_t stack_bottom;
void exit_process_wrapper();

void print_process() {
    uint32_t cur_tick = pit_get_ticks();
    printf("id  priority  state  time(s)\n");
    for (auto pid = 0; pid < MAX_PROCESSES_NUM; ++pid) {
        if (process_list[pid] != nullptr) {
            printf("%d   %d         %d         %d\n", pid, process_list[pid]->priority, process_list[pid]->state, (cur_tick - process_list[pid]->create_time) / 100);
        }
    }
}

void process_init() {
    init_scheduler();
    process_list[0] = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
    process_list[0]->kernel_stack_bottom = reinterpret_cast<void*>(stack_bottom);
    process_list[0]->prev = process_list[0]->next = nullptr;
    process_list[0]->pid = 0;
    process_list[0]->create_time = 0;
    process_list[0]->state = process_state::RUNNING;
    cur_process_id = 0;
}

uint32_t create_process(void* entry, void* args) {
    for (auto nid = 0; nid < MAX_PROCESSES_NUM; ++nid) {
        if (process_list[nid] == nullptr) {
            PCB*& new_process = process_list[nid];
            new_process = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
            memset(new_process, 0, sizeof(PCB));
            new_process->kernel_stack_bottom = kmalloc(4096);
            new_process->esp = (uintptr_t)(new_process->kernel_stack_bottom) + 4096;
            *((uintptr_t*)(new_process->esp - 4)) = reinterpret_cast<uintptr_t>(args);
            *((uintptr_t*)(new_process->esp - 8)) = reinterpret_cast<uintptr_t>(&exit_process_wrapper);
            *((uintptr_t*)(new_process->esp - 12)) = reinterpret_cast<uintptr_t>(entry);
            *((uintptr_t*)(new_process->esp - 16)) = 0x200;  // EFLAGS (popfl)
            *((uintptr_t*)(new_process->esp - 20)) = 0;      // ebx
            *((uintptr_t*)(new_process->esp - 24)) = 0;      // esi
            *((uintptr_t*)(new_process->esp - 28)) = 0;      // edi
            *((uintptr_t*)(new_process->esp - 32)) = 0;      // ebp  ← 栈顶，最先被 pop
            new_process->esp -= 32;
            new_process->pid = nid;
            new_process->create_time = pit_get_ticks();
            new_process->state = process_state::READY;
            insert_into_scheduling_queue(nid);
            return nid;
        }
    }
    return 0;
}

uint32_t exit_process(uint8_t pid) {
    if (pid == 0 || process_list[pid] == nullptr) return 1;
    remove_from_scheduling_queue(pid);
    PCB*& cur_process = process_list[pid];
    cur_process->state = process_state::ZOMBIE;
    // kfree(reinterpret_cast<void*>(cur_process->kernel_stack_bottom));
    // kfree(reinterpret_cast<void*>(cur_process));
    // cur_process = nullptr;
    yield();
    // 不应该执行到这里
    return 0;
}

void exit_process_wrapper() {
    exit_process(cur_process_id);
}

bool insert_into_process_queue(process_queue& queue, PCB* process) {
    if (process == nullptr || process->prev != nullptr || process->next != nullptr) {
        // 重复插入直接忽略
        return false;
    }
 
    if (queue) {
        process->next = queue;
        process->prev = queue->prev;
        queue->prev->next = process;
        queue->prev = process;
    } else {
        process->next = process;
        process->prev = process;
        queue = process;
    }
    return true;
}

void remove_from_process_queue(process_queue& queue, uint8_t pid) {
    // 不打算在这里检查pid对应的PCB是否在传入的queue中，调用者应该做检查
    PCB* cur_pcb = process_list[pid];
    PCB* prev_pcb = cur_pcb->prev;
    PCB* next_pcb = cur_pcb->next;
    if (cur_pcb == prev_pcb) {
        queue = nullptr;
        cur_pcb->prev = cur_pcb->next = nullptr;
        return;
    }
    
    if (prev_pcb) prev_pcb->next = next_pcb;
    if (next_pcb) next_pcb->prev = prev_pcb;

    if (queue == cur_pcb) {
        queue = cur_pcb->next;
    }

    cur_pcb->prev = cur_pcb->next = nullptr;
}