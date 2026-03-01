#include <kernel/schedule.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/hal.h>
#include <stdio.h>
#include <string.h>

constexpr uint8_t MAP_PRIORITY_TO_QUOTA[NUM_PRIORITY] = {32, 16, 8, 4, 2};
process_queue sche_queue_head[NUM_PRIORITY];

constexpr uint16_t RESETCNT_INITIAL = 500;
uint16_t resetcnt = RESETCNT_INITIAL;

void init_scheduler() {
    memset(sche_queue_head, 0, sizeof(sche_queue_head));
}

void insert_into_scheduling_queue(pid_t pid, uint8_t priority, bool set_quota) {
    if (insert_into_process_queue(sche_queue_head[priority], process_list[pid])) {
        process_list[pid]->priority = priority;
        if (set_quota) process_list[pid]->quota = MAP_PRIORITY_TO_QUOTA[priority];
    }
}

void remove_from_scheduling_queue(pid_t pid) {
    PCB* cur_pcb = process_list[pid];
    remove_from_process_queue(sche_queue_head[cur_pcb->priority], pid);
}

void move_all_to_top_priority() {
    for (int i = 1; i < NUM_PRIORITY; ++i) {
        if (!sche_queue_head[i - 1]) continue;
        if (sche_queue_head[i]) {
            sche_queue_head[i]->prev->next = sche_queue_head[i - 1]->next;
            sche_queue_head[i - 1]->next->prev = sche_queue_head[i]->prev;
            sche_queue_head[i]->prev = sche_queue_head[i - 1];
            sche_queue_head[i - 1]->next = sche_queue_head[i];
        } else {
            sche_queue_head[i] = sche_queue_head[i - 1];
        }
        sche_queue_head[i - 1] = nullptr;
    }
    PCB* head = sche_queue_head[MAX_PRIORITY];
    PCB* tail = head;
    do {
        tail->priority = MAX_PRIORITY;
        tail->quota = MAP_PRIORITY_TO_QUOTA[MAX_PRIORITY];
        tail = tail->next;
    } while (tail != head);
}

void schedule() {
    uint32_t flags;
    asm volatile ("pushfl; popl %0; cli" : "=r"(flags));

    PCB* cur_pcb = process_list[cur_process_id];
    cur_pcb->saved_eflags = flags;

    if (cur_pcb->state == process_state::RUNNING) {
        insert_into_scheduling_queue(cur_process_id, cur_pcb->priority, false);
        cur_pcb->state = process_state::READY;
        if (--(cur_pcb->quota) == 0) {
            if (cur_pcb->priority > 0) {
                insert_into_scheduling_queue(cur_process_id, cur_pcb->priority - 1);
            }
            cur_pcb->quota = MAP_PRIORITY_TO_QUOTA[cur_pcb->priority];
        }
    }
    if (--resetcnt == 0) {
        resetcnt = RESETCNT_INITIAL;
        move_all_to_top_priority();
    }
    PCB* chosen_process = nullptr;
    for (int i = NUM_PRIORITY - 1; i >= 0; --i) {
        if (sche_queue_head[i]) {
            chosen_process = sche_queue_head[i];
            sche_queue_head[i] = sche_queue_head[i]->next;
            break;
        }
    }
    if (!chosen_process) {
        chosen_process = process_list[0];
    }
    chosen_process->state = process_state::RUNNING;
    remove_from_scheduling_queue(chosen_process->pid);

    if (chosen_process->pid == cur_process_id) {
        asm volatile ("pushl %0; popfl" : : "r"(flags));
        return;
    }

    update_kernel_stack((uint32_t)chosen_process->kernel_stack_bottom + KERNEL_STACK_SIZE);
    process_switch_to(chosen_process->pid);

    flags = process_list[cur_process_id]->saved_eflags;
    asm volatile ("pushl %0; popfl" : : "r"(flags));

    if (process_list[cur_process_id]->to_exit) {
        exit_process(cur_process_id);
    }
}

void yield() {
    schedule();
}