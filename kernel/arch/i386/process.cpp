#include <kernel/process.h>
#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/schedule.h>
#include <kernel/spinlock.h>
#include <driver/pit.h>
#include <string.h>
#include <stdio.h>

PCB* process_list[MAX_PROCESSES_NUM] = {};
uint8_t cur_process_id = 0;
spinlock process_list_lock;

extern "C" void ret_to_user_mode();
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
    memset(process_list[0], 0, sizeof(PCB));
    process_list[0]->kernel_stack_bottom = reinterpret_cast<void*>(stack_bottom);
    process_list[0]->prev = process_list[0]->next = nullptr;
    process_list[0]->pid = 0;
    process_list[0]->saved_eflags = 0x202;
    process_list[0]->create_time = 0;
    process_list[0]->cr3 = vmm_get_cr3();
    process_list[0]->state = process_state::RUNNING;
    cur_process_id = 0;
}

constexpr uint32_t CODE_SPACE_ADDR = 0x04000000;
constexpr uint32_t CODE_STACK_TOP_ADDR = 0xBFF00000;

uint32_t create_user_process(void* code, uint32_t code_size, uint8_t priority) {
    spinlock_acquire(&process_list_lock);
    uint8_t newpid = 0;
    for (auto nid = 1; nid < MAX_PROCESSES_NUM; ++nid) {
        if (process_list[nid] == nullptr) {
            newpid = nid;
            break;
        }
    }
    if (newpid == 0) {
        spinlock_release(&process_list_lock);
        return 0;
    }

    uint32_t pd_addr_old = vmm_get_cr3();
    uint32_t pd_addr = vmm_create_page_directory();

    void* code_buf = kmalloc(code_size);
    memcpy(code_buf, code, code_size);

    asm volatile ("cli");
    vmm_switch(pd_addr);
    uint32_t pages_needed = (code_size + 4095) / 4096;
    for (uint32_t i = 0; i < pages_needed; i++) {
        void* phys = pmm_alloc(1);
        vmm_map_page((uintptr_t)phys, CODE_SPACE_ADDR + i * 4096, 6);
    }
    memcpy((void*)CODE_SPACE_ADDR, code_buf, code_size);
    kfree(code_buf);

    for (uint32_t i = 0; i < 16; i++) {
        void* stack_space = pmm_alloc(1);
        vmm_map_page((uintptr_t)stack_space, CODE_STACK_TOP_ADDR - (16 - i) * 4096, 6);
    }

    PCB*& new_process = process_list[newpid];
    new_process = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
    memset(new_process, 0, sizeof(PCB));
    new_process->kernel_stack_bottom = kmalloc(KERNEL_STACK_SIZE);
    new_process->esp = (uintptr_t)(new_process->kernel_stack_bottom) + KERNEL_STACK_SIZE;
    
    // 内核栈
    *((uintptr_t*)(new_process->esp - 4)) = 0x23; // SS
    *((uintptr_t*)(new_process->esp - 8)) = CODE_STACK_TOP_ADDR; // ESP
    *((uintptr_t*)(new_process->esp - 12)) = 0x202; // EFLAG
    *((uintptr_t*)(new_process->esp - 16)) = 0x1B; // CS
    *((uintptr_t*)(new_process->esp - 20)) = CODE_SPACE_ADDR; // EIP
    *((uintptr_t*)(new_process->esp - 24)) = reinterpret_cast<uintptr_t>(&ret_to_user_mode);
    *((uintptr_t*)(new_process->esp - 28)) = 0;      // ebx
    *((uintptr_t*)(new_process->esp - 32)) = 0;      // esi
    *((uintptr_t*)(new_process->esp - 36)) = 0;      // edi
    *((uintptr_t*)(new_process->esp - 40)) = 0;      // ebp  ← 栈顶，最先被 pop
    new_process->esp -= 40;
    new_process->saved_eflags = 0x202;
    new_process->pid = newpid;
    new_process->create_time = pit_get_ticks();
    new_process->cr3 = pd_addr;
    new_process->state = process_state::READY;
    insert_into_scheduling_queue(newpid, priority);
    vmm_switch(pd_addr_old);
    asm volatile ("sti");
    spinlock_release(&process_list_lock);

    return newpid;
}

uint32_t create_process(void* entry, void* args) {
    spinlock_acquire(&process_list_lock);
    for (auto nid = 1; nid < MAX_PROCESSES_NUM; ++nid) {
        if (process_list[nid] == nullptr) {
            PCB*& new_process = process_list[nid];
            new_process = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
            memset(new_process, 0, sizeof(PCB));
            new_process->kernel_stack_bottom = kmalloc(KERNEL_STACK_SIZE);
            new_process->esp = (uintptr_t)(new_process->kernel_stack_bottom) + KERNEL_STACK_SIZE;
            *((uintptr_t*)(new_process->esp - 4)) = reinterpret_cast<uintptr_t>(args);
            *((uintptr_t*)(new_process->esp - 8)) = reinterpret_cast<uintptr_t>(&exit_process_wrapper);
            *((uintptr_t*)(new_process->esp - 12)) = reinterpret_cast<uintptr_t>(entry);
            *((uintptr_t*)(new_process->esp - 16)) = 0;      // ebx
            *((uintptr_t*)(new_process->esp - 20)) = 0;      // esi
            *((uintptr_t*)(new_process->esp - 24)) = 0;      // edi
            *((uintptr_t*)(new_process->esp - 28)) = 0;      // ebp  ← 栈顶，最先被 pop
            new_process->esp -= 28;
            new_process->saved_eflags = 0x202;
            new_process->pid = nid;
            new_process->cr3 = vmm_get_cr3();
            new_process->create_time = pit_get_ticks();
            new_process->state = process_state::READY;
            insert_into_scheduling_queue(nid);
            spinlock_release(&process_list_lock);
            return nid;
        }
    }
    spinlock_release(&process_list_lock);
    return 0;
}

void free_pcb(PCB*& process) {
    kfree(reinterpret_cast<void*>(process->kernel_stack_bottom));
    kfree(reinterpret_cast<void*>(process));
    // todo：如果是用户态进程，需要销毁用户空间（CR3）
    process = nullptr;
}

uint32_t exit_process(uint8_t pid) {
    printf("exiting: %d\n", pid);
    if (pid == 0 || process_list[pid] == nullptr) return 1;
    PCB*& cur_process = process_list[pid];
    if (pid != cur_process_id) {
		remove_from_scheduling_queue(pid);
        free_pcb(cur_process);
        return 0;
    } 
    cur_process->state = process_state::ZOMBIE;
    insert_into_process_recycle_queue(cur_process);
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

process_queue process_recycle_queue;

void insert_into_process_recycle_queue(PCB* process) {
    insert_into_process_queue(process_recycle_queue, process);
}

void do_process_recycle() {
    while (process_recycle_queue) {
        uint8_t pid = process_recycle_queue->pid;
        remove_from_process_queue(process_recycle_queue, process_recycle_queue->pid);
        free_pcb(process_list[pid]);
    }
}