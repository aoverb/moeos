#include <kernel/process.h>
#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/schedule.h>
#include <string.h>
#include <stdio.h>

PCB* process_list[MAX_PROCESSES_NUM] = {};
uint8_t cur_process_id = 0;

extern uintptr_t stack_bottom;
void exit_process_wrapper();

void process_init() {
    init_scheduler();
    process_list[0] = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
    process_list[0]->kernel_stack_bottom = reinterpret_cast<void*>(stack_bottom);
    process_list[0]->prev = process_list[0]->next = nullptr;
    process_list[0]->pid = 0;
    cur_process_id = 0;
    insert_into_scheduling_queue(0);
}

uint32_t create_process(void* entry) {
    for (auto nid = 0; nid < MAX_PROCESSES_NUM; ++nid) {
        if (process_list[nid] == nullptr) {
            PCB*& new_process = process_list[nid];
            new_process = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
            memset(new_process, 0, sizeof(PCB));
            new_process->kernel_stack_bottom = kmalloc(4096);
            new_process->esp = (uintptr_t)(new_process->kernel_stack_bottom) + 4096;
            *((uintptr_t*)(new_process->esp - 4)) = reinterpret_cast<uintptr_t>(&exit_process_wrapper);
            *((uintptr_t*)(new_process->esp - 8)) = reinterpret_cast<uintptr_t>(entry);
            *((uintptr_t*)(new_process->esp - 12)) = 0x200;  // EFLAGS (popfl)
            *((uintptr_t*)(new_process->esp - 16)) = 0;      // ebx
            *((uintptr_t*)(new_process->esp - 20)) = 0;      // esi
            *((uintptr_t*)(new_process->esp - 24)) = 0;      // edi
            *((uintptr_t*)(new_process->esp - 28)) = 0;      // ebp  ← 栈顶，最先被 pop
            new_process->esp -= 28;
            new_process->pid = nid;
            insert_into_scheduling_queue(nid);
            process_switch_to(nid);
            return nid;
        }
    }
    return 0;
}

uint32_t exit_process(uint8_t pid) {
    if (pid == 0 || process_list[pid] == nullptr) return 1;
    remove_from_scheduling_queue(pid);
    PCB*& cur_process = process_list[pid];
    kfree(reinterpret_cast<void*>(cur_process->kernel_stack_bottom));
    kfree(reinterpret_cast<void*>(cur_process));
    cur_process = nullptr;
    yield();
    // 不应该执行到这里
    return 0;
}

void exit_process_wrapper() {
    exit_process(cur_process_id);
}