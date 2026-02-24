#include <kernel/schedule.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <stdio.h>
#include <string.h>

constexpr uint8_t NUM_PRIORITY = 5;
constexpr uint8_t MAX_PRIORITY = NUM_PRIORITY - 1;
constexpr uint8_t MAP_PRIORITY_TO_QUOTA[NUM_PRIORITY] = {32, 16, 8, 4, 2};
PCB* sche_queue_head[NUM_PRIORITY];

constexpr uint8_t RESETCNT_INITIAL = 500;
uint8_t resetcnt = RESETCNT_INITIAL;

void init_scheduler() {
    memset(sche_queue_head, 0, sizeof(sche_queue_head));
}

void insert_into_queue(PCB* process, uint8_t priority) {
    if (process->prev != nullptr || process->next != nullptr) {
        // 重复插入直接忽略
        return;
    }
    process->priority = priority;
    process->quota = MAP_PRIORITY_TO_QUOTA[priority];
    if (sche_queue_head[priority]) {
        process->next = sche_queue_head[priority];
        process->prev = sche_queue_head[priority]->prev;
        sche_queue_head[priority]->prev->next = process;
        sche_queue_head[priority]->prev = process;
    } else {
        process->next = process;
        process->prev = process;
    }

    sche_queue_head[priority] = process;
}

void insert_into_scheduling_queue(uint8_t pid) {
    insert_into_queue(process_list[pid], MAX_PRIORITY);
}

void remove_from_scheduling_queue(uint8_t pid) {
    PCB* cur_pcb = process_list[pid];
    PCB* prev_pcb = cur_pcb->prev;
    PCB* next_pcb = cur_pcb->next;
    if (cur_pcb == prev_pcb) {
        sche_queue_head[cur_pcb->priority] = nullptr;
        cur_pcb->prev = cur_pcb->next = nullptr;
        return;
    }
    
    if (prev_pcb) prev_pcb->next = next_pcb;
    if (next_pcb) next_pcb->prev = prev_pcb;

    if (sche_queue_head[cur_pcb->priority] == cur_pcb) {
        sche_queue_head[cur_pcb->priority] = cur_pcb->next;
    }

    cur_pcb->prev = cur_pcb->next = nullptr;
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
    PCB* cur_pcb = process_list[cur_process_id];
    if (--(cur_pcb->quota) == 0) {
        if (cur_pcb->priority > 0) {
            remove_from_scheduling_queue(cur_process_id);
            insert_into_queue(cur_pcb, cur_pcb->priority - 1);
        }
        cur_pcb->quota = MAP_PRIORITY_TO_QUOTA[cur_pcb->priority];
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
    process_switch_to(chosen_process->pid);
}

void yield() {
    schedule();
}