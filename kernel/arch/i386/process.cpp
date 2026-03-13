#include <kernel/process.hpp>
#include <kernel/mm.hpp>
#include <kernel/panic.h>
#include <kernel/schedule.hpp>
#include <kernel/spinlock.hpp>
#include <driver/pit.h>
#include <string.h>
#include <stdio.h>
#include <elf.h>

PCB* process_list[MAX_PROCESSES_NUM] = {};
pid_t cur_process_id = 0;
spinlock process_list_lock;

extern "C" int v_open(PCB* proc, const char* path, uint8_t mode);
extern "C" int v_dup_to(PCB* src_proc, int fd_src, PCB* dst_proc, int fd_dst);
extern "C" int v_close(PCB* proc, int fd_pos);

extern "C" void ret_to_user_mode();
extern "C" void schedule_tail_restore();
extern uintptr_t stack_bottom;
void exit_process_wrapper();

void print_process() {
    uint32_t flags = spinlock_acquire(&process_list_lock);
    uint32_t cur_tick = pit_get_ticks();
    printf("id  priority  state  time(s)\n");
    for (auto pid = 0; pid < MAX_PROCESSES_NUM; ++pid) {
        if (process_list[pid] != nullptr) {
            printf("%d   %d         %d         %d\n", pid, process_list[pid]->priority, process_list[pid]->state, (cur_tick - process_list[pid]->create_time) / 100);
        }
    }
    spinlock_release(&process_list_lock, flags);
}

void free_pcb(PCB*& process) {
    // 调用者必须持有 process_list_lock
    for (int i = 0; i < MAX_FD_NUM; ++i) {
        v_close(process, i);
    } 
    
    kfree(reinterpret_cast<void*>(process->kernel_stack_bottom));
    kfree(reinterpret_cast<void*>(process));
    // todo：如果是用户态进程，需要销毁用户空间（CR3）
    process = nullptr;
}

int waitpid(pid_t child) {
    // 不能用 SpinlockGuard：yield() 前必须手动释放锁
    uint32_t flags = spinlock_acquire(&process_list_lock);

    if (child < 0 || child >= MAX_PROCESSES_NUM ||
        !process_list[child] || process_list[child]->parent_pid != cur_process_id) {
        spinlock_release(&process_list_lock, flags);
        return -1;
    }

    PCB* child_pcb = process_list[child];

    if (child_pcb->state == process_state::ZOMBIE) {
        // 子进程已经退出了，直接回收
        int exit_code = child_pcb->exit_code;
        free_pcb(process_list[child]);
        spinlock_release(&process_list_lock, flags);
        return exit_code;
    }

    process_list[cur_process_id]->state = process_state::WAITING;
    insert_into_process_queue(child_pcb->waiting_queue, process_list[cur_process_id]);
    spinlock_release(&process_list_lock, flags);
    yield();

    flags = spinlock_acquire(&process_list_lock);
    int exit_code = child_pcb->exit_code;
    free_pcb(process_list[child]);
    spinlock_release(&process_list_lock, flags);
    return exit_code;
}

PCB* init_pcb(pid_t newpid) {
    // 调用者必须持有 process_list_lock
    PCB*& new_process = process_list[newpid];
    new_process = reinterpret_cast<PCB*>(kmalloc(sizeof(PCB)));
    memset(new_process, 0, sizeof(PCB));
    new_process->pid = newpid;
    return new_process;
}

void prepare_pcb_for_new_process(PCB*& new_process) {
    // 调用者必须持有 process_list_lock
    new_process->saved_eflags = 0x202;
    new_process->create_time = pit_get_ticks();
    new_process->state = process_state::READY;
    new_process->parent_pid = cur_process_id;
    new_process->waiting_queue = nullptr;
    new_process->fd_num = 0;
    new_process->to_exit = 0;
    new_process->plock.locked = 0;
    new_process->signal = 0;
    strcpy(new_process->cwd, process_list[cur_process_id]->cwd);
    if (cur_process_id == 0) {
        v_open(new_process, "/dev/console", O_RDONLY);
        v_open(new_process, "/dev/console", O_WRONLY);
    } else {
        v_dup_to(process_list[cur_process_id], 0, new_process, 0);
        v_dup_to(process_list[cur_process_id], 1, new_process, 1);
    }
}

void process_init() {
    printf("process initializing...");
    init_scheduler();
    PCB* new_process = init_pcb(0);
    cur_process_id = 0;
    prepare_pcb_for_new_process(new_process);
    strcpy(new_process->name, KERNEL_PROC_NAME_IDLE);
    strcpy(new_process->cwd, "/");
    new_process->cr3 = vmm_get_cr3();
    new_process->state = process_state::RUNNING;
    // 0号进程没有用户态，esp为0无所谓
    // 调度到别的进程的时候会把这个esp自动刷新
    // kernel_bottom 同样不需要刷新，调用free_pcb销毁内核栈用
    // 但是引导程序的内核栈是不会也不应该被销毁的
    printf("OK\n");
}

constexpr uint32_t CODE_SPACE_ADDR = 0x04000000;
constexpr uint32_t USER_STACK_TOP_ADDR = 0xBFF00000;

pid_t get_new_pid() {
    // 调用者必须持有 process_list_lock
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

void copy_remaps_to_kernel_buffer(fd_remap* remaps, int remap_cnt, fd_remap*& remaps_buf) {
    if (!remaps || !remap_cnt) return;
    remaps_buf = (fd_remap*)(kmalloc(sizeof(fd_remap) * remap_cnt));
    for (int i = 0; i < remap_cnt; ++i) {
        remaps_buf[i].child_fd = remaps[i].child_fd;
        remaps_buf[i].parent_fd = remaps[i].parent_fd;
    }
}

void construct_args_for_user_stack(int argc, uint32_t* arg_lens, char** arg_bufs,
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

uintptr_t create_user_stack(uint32_t page_size) {
    uintptr_t stack_top_addr = USER_STACK_TOP_ADDR;
    for (uint32_t i = 0; i < page_size; i++) {
        void* stack_space = pmm_alloc(1 << 12);
        vmm_map_page((uintptr_t)stack_space, stack_top_addr - (page_size - i) * 4096, 6);
    }
    return stack_top_addr;
}

void remap_fd(pid_t newpid, fd_remap* remaps, int remap_cnt) {
    if (!remaps || !remap_cnt) return;
    for (int i = 0; i < remap_cnt; ++i) {
        v_dup_to(process_list[cur_process_id], remaps[i].parent_fd,
                  process_list[newpid], remaps[i].child_fd);
    }
}

void init_kernel_stack(PCB*& new_process, uint32_t size, uintptr_t user_stack_pointer, uintptr_t entry) {
    new_process->kernel_stack_bottom = kmalloc(size);
    new_process->esp = (uintptr_t)(new_process->kernel_stack_bottom) + size;
    // 内核栈
    *((uintptr_t*)(new_process->esp - 4)) = 0x23; // SS
    *((uintptr_t*)(new_process->esp - 8)) = user_stack_pointer; // ESP
    *((uintptr_t*)(new_process->esp - 12)) = 0x202; // EFLAG
    *((uintptr_t*)(new_process->esp - 16)) = 0x1B; // CS
    *((uintptr_t*)(new_process->esp - 20)) = entry; // EIP
    *((uintptr_t*)(new_process->esp - 24)) = reinterpret_cast<uintptr_t>(&ret_to_user_mode);
    *((uintptr_t*)(new_process->esp - 28)) = 0;      // ebx
    *((uintptr_t*)(new_process->esp - 32)) = 0;      // esi
    *((uintptr_t*)(new_process->esp - 36)) = 0;      // edi
    *((uintptr_t*)(new_process->esp - 40)) = 0;      // ebp
    new_process->esp -= 40;
}

pid_t exec(const char* name, void* code, uint32_t code_size, uint8_t priority, int argc, char** argv,
    fd_remap* remaps, int remap_cnt) {
    if (!verify_elf(code, code_size)) {
        return 0;
    }

    void* code_buf = copy_image_to_kernel_buffer(code, code_size);

    uint32_t* arg_lens;
    char** arg_bufs;
    copy_args_to_kernel_buffer(argc, argv, arg_lens, arg_bufs);

    fd_remap* remaps_buf = nullptr;
    copy_remaps_to_kernel_buffer(remaps, remap_cnt, remaps_buf);
    pid_t newpid;
    PCB* new_pcb;
    {
        SpinlockGuard guard(process_list_lock);
        newpid = get_new_pid();
        if (newpid == 0) {
            kfree(code_buf);
            if(remaps_buf) kfree(remaps_buf);
            return 0;
        }
        new_pcb = init_pcb(newpid);
    }

    spinlock pd_lock;
    SpinlockGuard pdGuard(pd_lock);

    uint32_t pd_addr_old = vmm_get_cr3();
    uint32_t pd_addr = vmm_create_page_directory();
    vmm_switch(pd_addr);

    uint32_t entry = 0;
    uint32_t heap_addr = 0;
    if (!construct_user_space_by_elf_image(code_buf, code_size, entry, heap_addr)) {
        kfree(code_buf);
        vmm_switch(pd_addr_old);
        SpinlockGuard guard(process_list_lock);
        free_pcb(process_list[newpid]);
        if(remaps_buf) kfree(remaps_buf);
        return 0;
    }
    kfree(code_buf);
    uintptr_t sp = create_user_stack(USER_STACK_PAGE_SIZE);
    construct_args_for_user_stack(argc, arg_lens, arg_bufs, sp);

    {
        SpinlockGuard guard(process_list_lock);
        prepare_pcb_for_new_process(new_pcb);
        new_pcb->cr3 = pd_addr;
        new_pcb->heap_start = heap_addr;
        new_pcb->heap_break = heap_addr;
        strcpy(new_pcb->name, name);
        remap_fd(newpid, remaps_buf, remap_cnt);
        if(remaps_buf) kfree(remaps_buf);

        init_kernel_stack(new_pcb, KERNEL_STACK_SIZE, sp, entry);
        insert_into_scheduling_queue(newpid, priority);
    }

    vmm_switch(pd_addr_old);
    return newpid;
}
pid_t create_process(const char* name, void* entry, void* args) {
    SpinlockGuard guard(process_list_lock);

    pid_t newpid = get_new_pid();
    if (newpid == 0) return 0;

    PCB* new_process = init_pcb(newpid);
    prepare_pcb_for_new_process(new_process);
    strcpy(new_process->name, name);
    new_process->cr3 = vmm_get_cr3();

    new_process->kernel_stack_bottom = kmalloc(KERNEL_STACK_SIZE);
    new_process->esp = (uintptr_t)(new_process->kernel_stack_bottom) + KERNEL_STACK_SIZE;

    *((uintptr_t*)(new_process->esp - 4))  = reinterpret_cast<uintptr_t>(args);
    *((uintptr_t*)(new_process->esp - 8))  = reinterpret_cast<uintptr_t>(&exit_process_wrapper);
    *((uintptr_t*)(new_process->esp - 12)) = reinterpret_cast<uintptr_t>(entry);
    *((uintptr_t*)(new_process->esp - 16)) = 0x202;
    *((uintptr_t*)(new_process->esp - 20)) = reinterpret_cast<uintptr_t>(schedule_tail_restore);
    *((uintptr_t*)(new_process->esp - 24)) = 0;  // ebx
    *((uintptr_t*)(new_process->esp - 28)) = 0;  // esi
    *((uintptr_t*)(new_process->esp - 32)) = 0;  // edi
    *((uintptr_t*)(new_process->esp - 36)) = 0;  // ebp
    new_process->esp -= 36;

    insert_into_scheduling_queue(newpid);
    return newpid;
}

uint32_t exit_process(pid_t pid, int exit_code) {
    uint32_t flags = spinlock_acquire(&process_list_lock);
    
    if (pid == 0 || process_list[pid] == nullptr) {
        spinlock_release(&process_list_lock, flags);
        return 1;
    }

    PCB*& exiting_process = process_list[pid];
    if (!exiting_process->to_exit) {
        // 当前进程还没被指派退出，使用传入的退出码
        exiting_process->exit_code = exit_code;
    }
    if (pid != cur_process_id) { // 要退出的进程不是自己的话
		exiting_process->to_exit = 1; // 不要直接清理这个进程的空间，告诉进程自己将要被退出就好
        spinlock_release(&process_list_lock, flags);
        return 0;
    }
    PCB* itr;
    while (itr = exiting_process->waiting_queue) {
        itr->state = process_state::READY;
        remove_from_process_queue(exiting_process->waiting_queue, itr->pid);
        insert_into_scheduling_queue(itr->pid);
    }
    exiting_process->state = process_state::ZOMBIE;
    spinlock_release(&process_list_lock, flags);
    yield();
    // 不应该执行到这里
    return 0;
}

void exit_process_wrapper() {
    exit_process(cur_process_id, 0);
}

bool insert_into_process_queue(process_queue& queue, PCB* process) {
    // 调用者必须持有 process_list_lock
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
    // 调用者必须持有 process_list_lock
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
