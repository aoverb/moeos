#include <kernel/process.h>
#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/schedule.h>
#include <kernel/spinlock.h>
#include <driver/pit.h>
#include <string.h>
#include <stdio.h>

PCB* process_list[MAX_PROCESSES_NUM] = {};
pid_t cur_process_id = 0;
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

void free_pcb(PCB*& process) {
    kfree(reinterpret_cast<void*>(process->kernel_stack_bottom));
    kfree(reinterpret_cast<void*>(process));
    // todo：如果是用户态进程，需要销毁用户空间（CR3）
    process = nullptr;
}

int waitpid(pid_t child) {
    if (child < 0 || child > MAX_PROCESSES_NUM ||
        !process_list[child] || process_list[child]->parent_pid != cur_process_id) {
        return -1;
    }
    PCB* child_pcb = process_list[child];

    asm volatile("cli");
    if (child_pcb->state == process_state::ZOMBIE) {
        // 子进程已经退出了，直接回收
        asm volatile("sti");
        int exit_code = child_pcb->exit_code;
        free_pcb(process_list[child]);
        return exit_code;
    }
    process_list[cur_process_id]->state = process_state::WAITING;
    insert_into_process_queue(child_pcb->waiting_queue, process_list[cur_process_id]);
    yield();

    int exit_code = child_pcb->exit_code;
    free_pcb(process_list[child]);
    return exit_code;
}

void process_init() {
    init_scheduler();
    process_list[0] = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
    memset(process_list[0], 0, sizeof(PCB));
    process_list[0]->kernel_stack_bottom = reinterpret_cast<void*>(stack_bottom);
    process_list[0]->prev = process_list[0]->next = nullptr;
    process_list[0]->pid = 0;
    process_list[0]->fd_num = 0;
    process_list[0]->to_exit = 0;
    process_list[0]->parent_pid = 0;
    process_list[0]->waiting_queue = nullptr;
    strcpy(process_list[0]->cwd, "/");
    process_list[0]->saved_eflags = 0x202;
    process_list[0]->create_time = 0;
    process_list[0]->cr3 = vmm_get_cr3();
    process_list[0]->state = process_state::RUNNING;
    cur_process_id = 0;
}

constexpr uint32_t CODE_SPACE_ADDR = 0x04000000;
constexpr uint32_t CODE_STACK_TOP_ADDR = 0xBFF00000;
// ============ 临时方案：.bss 处理（解析 ELF 后可删除） ============
constexpr uint32_t BSS_EXTRA_PAGES = 4; // 额外给 .bss 的页数

static uint32_t calc_total_pages(uint32_t code_size) {
    return (code_size + 4095) / 4096 + BSS_EXTRA_PAGES;
}

static void load_code_and_clear_bss(uintptr_t dest, void* code_buf, uint32_t code_size, uint32_t total_pages) {
    memcpy((void*)dest, code_buf, code_size);
    memset((void*)(dest + code_size), 0, total_pages * 4096 - code_size);
}
// ============ 临时方案结束 ============

pid_t get_new_pid() {
    pid_t newpid = 0;
    for (auto nid = 1; nid < MAX_PROCESSES_NUM; ++nid) {
        if (process_list[nid] == nullptr) {
            newpid = nid;
            break;
        }
    }
    return newpid;
}

void copy_args_to_kernel_buffer(int argc, char** argv, uint32_t*& arg_lens, char**& arg_bufs) {
    arg_lens = (uint32_t*)kmalloc(argc * sizeof(uint32_t));
    arg_bufs = (char**)kmalloc(argc * sizeof(char*));
    for (int i = 0; i < argc; i++) {
        arg_lens[i] = strlen(argv[i]) + 1;
        arg_bufs[i] = (char*)kmalloc(arg_lens[i]);
        memcpy(arg_bufs[i], argv[i], arg_lens[i]);
    }
}

void construct_args_from_kernel_buffer(int argc, uint32_t* arg_lens, char** arg_bufs,
    uintptr_t& user_stack_pointer) {
    uintptr_t* user_argv_ptrs = (uintptr_t*)kmalloc(argc * sizeof(uintptr_t));
    for (int i = argc - 1; i >= 0; i--) {
        user_stack_pointer -= arg_lens[i];
        memcpy((void*)user_stack_pointer, arg_bufs[i], arg_lens[i]);
        user_argv_ptrs[i] = user_stack_pointer;
        kfree(arg_bufs[i]);
    }
    kfree(arg_bufs);
    kfree(arg_lens);

    user_stack_pointer &= ~0x3; // 4 字节对齐
 
    // argv[argc] = NULL
    user_stack_pointer -= 4;
    *((uintptr_t*)user_stack_pointer) = 0;
 
    // argv[0] ~ argv[argc-1]
    for (int i = argc - 1; i >= 0; i--) {
        user_stack_pointer -= 4;
        *((uintptr_t*)user_stack_pointer) = user_argv_ptrs[i];
    }
    uintptr_t argv_array_addr = user_stack_pointer;
    kfree(user_argv_ptrs);
    user_stack_pointer -= 4;
    *((uintptr_t*)user_stack_pointer) = argv_array_addr;  // argv
    user_stack_pointer -= 4;
    *((uintptr_t*)user_stack_pointer) = (uintptr_t)argc;  // argc
}

void* copy_image_to_kernel_buffer(void* code, uint32_t code_size) {
    void* code_buf = kmalloc(code_size);
    memcpy(code_buf, code, code_size);
    return code_buf;
}

void copy_image_from_kernel_buffer(void* code_buf, uint32_t code_size) {
    uint32_t total_pages = calc_total_pages(code_size);
    for (uint32_t i = 0; i < total_pages; i++) {
        void* phys = pmm_alloc(1);
        vmm_map_page((uintptr_t)phys, CODE_SPACE_ADDR + i * 4096, 6);
    }
    load_code_and_clear_bss(CODE_SPACE_ADDR, code_buf, code_size, total_pages);
    kfree(code_buf);
}

uintptr_t create_user_stack(uint32_t page_size) {
    uintptr_t stack_top_addr = CODE_STACK_TOP_ADDR;
    for (uint32_t i = 0; i < page_size; i++) {
        void* stack_space = pmm_alloc(1);
        vmm_map_page((uintptr_t)stack_space, stack_top_addr - (16 - i) * 4096, 6);
    }
    return stack_top_addr;
}

void init_pcb_for_new_process(pid_t newpid, uintptr_t sp, uint32_t pd_addr, uint32_t code_size) {
    PCB*& new_process = process_list[newpid];
    new_process = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
    memset(new_process, 0, sizeof(PCB));
    new_process->kernel_stack_bottom = kmalloc(KERNEL_STACK_SIZE);
    new_process->esp = (uintptr_t)(new_process->kernel_stack_bottom) + KERNEL_STACK_SIZE;
    
    // 内核栈
    *((uintptr_t*)(new_process->esp - 4)) = 0x23; // SS
    *((uintptr_t*)(new_process->esp - 8)) = CODE_STACK_TOP_ADDR; // ESP
    *((uintptr_t*)(new_process->esp - 8)) = sp; // ESP
    *((uintptr_t*)(new_process->esp - 12)) = 0x202; // EFLAG
    *((uintptr_t*)(new_process->esp - 16)) = 0x1B; // CS
    *((uintptr_t*)(new_process->esp - 20)) = CODE_SPACE_ADDR; // EIP
    *((uintptr_t*)(new_process->esp - 24)) = reinterpret_cast<uintptr_t>(&ret_to_user_mode);
    *((uintptr_t*)(new_process->esp - 28)) = 0;      // ebx
    *((uintptr_t*)(new_process->esp - 32)) = 0;      // esi
    *((uintptr_t*)(new_process->esp - 36)) = 0;      // edi
    *((uintptr_t*)(new_process->esp - 40)) = 0;      // ebp
    new_process->esp -= 40;
    new_process->saved_eflags = 0x202;
    new_process->pid = newpid;
    new_process->create_time = pit_get_ticks();
    new_process->cr3 = pd_addr;

    // heap_start 也要考虑 .bss 额外页
    uint32_t total_pages = calc_total_pages(code_size);
    new_process->heap_start = CODE_SPACE_ADDR + total_pages * 4096;
    new_process->heap_break = new_process->heap_start;

    new_process->state = process_state::READY;
    new_process->parent_pid = cur_process_id;
    new_process->waiting_queue = nullptr;
    new_process->fd_num = 0;
    new_process->to_exit = 0;
    strcpy(new_process->cwd, process_list[cur_process_id]->cwd);
}

pid_t exec(void* code, uint32_t code_size, uint8_t priority, int argc, char** argv) {
    uint32_t saved_eflags = spinlock_acquire(&process_list_lock);
    pid_t newpid = get_new_pid();

    if (newpid == 0) {
        spinlock_release(&process_list_lock, saved_eflags);
        return 0;
    }

    void* code_buf = copy_image_to_kernel_buffer(code, code_size);

    uint32_t* arg_lens;
    char** arg_bufs;
    copy_args_to_kernel_buffer(argc, argv, arg_lens, arg_bufs);

    uint32_t pd_addr_old = vmm_get_cr3();
    uint32_t pd_addr = vmm_create_page_directory();
    vmm_switch(pd_addr);

    copy_image_from_kernel_buffer(code_buf, code_size);
    
    uintptr_t sp = create_user_stack(USER_STACK_PAGE_SIZE);
    construct_args_from_kernel_buffer(argc, arg_lens, arg_bufs, sp);

    init_pcb_for_new_process(newpid, sp, pd_addr, code_size);
    insert_into_scheduling_queue(newpid, priority);

    vmm_switch(pd_addr_old);

    spinlock_release(&process_list_lock, saved_eflags);
    return newpid;
}

pid_t create_process(void* entry, void* args) {
    uint32_t saved_eflags = spinlock_acquire(&process_list_lock);
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
            new_process->fd_num = 0;
            new_process->to_exit = 0;
            new_process->parent_pid = cur_process_id;
            new_process->waiting_queue = nullptr;
            strcpy(new_process->cwd, process_list[cur_process_id]->cwd);
            new_process->state = process_state::READY;

            insert_into_scheduling_queue(nid);
            spinlock_release(&process_list_lock, saved_eflags);
            return nid;
        }
    }
    spinlock_release(&process_list_lock, saved_eflags);
    return 0;
}

uint32_t exit_process(pid_t pid, int exit_code) {
    if (pid == 0 || process_list[pid] == nullptr) return 1;
    PCB*& exiting_process = process_list[pid];
    if (!exiting_process->to_exit) {
        // 当前进程还没被指派退出，使用传入的退出码
        exiting_process->exit_code = exit_code;
    }
    if (pid != cur_process_id) { // 要退出的进程不是自己的话
		exiting_process->to_exit = 1; // 不要直接清理这个进程的空间，告诉进程自己将要被退出就好
        return 0;
    }
    asm volatile("cli");
    PCB* itr;
    while (itr = exiting_process->waiting_queue) {
        itr->state = process_state::READY;
        remove_from_process_queue(exiting_process->waiting_queue, itr->pid);
        insert_into_scheduling_queue(itr->pid);
    }
    exiting_process->state = process_state::ZOMBIE;
    yield();
    // 不应该执行到这里
    return 0;
}

void exit_process_wrapper() {
    exit_process(cur_process_id, 0);
}

bool insert_into_process_queue(process_queue& queue, PCB* process) {
    if (process == nullptr || process->prev != nullptr || process->next != nullptr) {
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

void remove_from_process_queue(process_queue& queue, pid_t pid) {
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
