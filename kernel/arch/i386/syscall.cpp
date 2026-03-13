#include <kernel/syscall.h>
#include <kernel/mm.hpp>
#include <syscall_def.hpp>
#include <kernel/process.hpp>
#include <driver/vfs.hpp>
#include <driver/pipefs.hpp>
#include <kernel/tty.h>
#include <kernel/spinlock.hpp>
#include <kernel/timer.hpp>
#include <driver/pit.h>
#include <kernel/schedule.hpp>

#include <poll.h>
#include <string.h>
#include <stdio.h>

constexpr uint32_t MAX_SYSCALL = 255;
syscall_handler_t syscall_table[255] = { nullptr };

// CLOSE(ebx = exit_code)
int sys_exit(interrupt_frame* reg) {
    int exit_code = static_cast<int>(reg->ebx);
    exit_process(cur_process_id, exit_code);
    return 0;
}

int sys_terminal_write(interrupt_frame* reg) {
    terminal_write((const char*)reg->ebx , strlen((const char*)reg->ebx));
    return 0;
}

int sys_terminal_set_text_color(interrupt_frame* reg) {
    terminal_setcolor(reg->ebx);
    return 0;
}

int sys_terminal_clear(interrupt_frame*) {
    terminal_clear();
    return 0;
}

int sys_terminal_getline(interrupt_frame* reg) {
    return getline(reinterpret_cast<char*>(reg->ebx), reg->ecx);
}

static PCB* current_pcb() {
    return process_list[cur_process_id];
}

// ACCEPT(ebx = fd, ecx = peeraddr, edx = size)  → returns 0 if succeeded
int sys_accept(interrupt_frame* reg) {
    int fd              = static_cast<int>(reg->ebx);
    sockaddr* peeraddr  = reinterpret_cast<sockaddr*>(reg->ecx);
    size_t* size        = reinterpret_cast<size_t*>(reg->edx);
    return v_accept(current_pcb(), fd, peeraddr, size);
}

// LISTEN(ebx = fd, ecx = queue_length)  → returns 0 if succeeded
int sys_listen(interrupt_frame* reg) {
    int fd              = static_cast<int>(reg->ebx);
    size_t queue_length = static_cast<size_t>(reg->ecx);
    return v_listen(current_pcb(), fd, queue_length);
}

// SENDTO(ebx = fd, ecx = buffer, edx = size, esi = sockaddr)  → returns 0 if succeeded
int sys_sendto(interrupt_frame* reg) {
    int fd              = static_cast<int>(reg->ebx);
    char* buffer        = reinterpret_cast<char*>(reg->ecx);
    uint32_t size       = reg->edx;
    sockaddr* peeraddr  = reinterpret_cast<sockaddr*>(reg->esi);
    return v_sendto(current_pcb(), fd, buffer, size, peeraddr);
}

// RECVFROM(ebx = fd, ecx = buffer, edx = size, esi = sockaddr)  → returns 0 if succeeded
int sys_recvfrom(interrupt_frame* reg) {
    int fd              = static_cast<int>(reg->ebx);
    char* buffer        = reinterpret_cast<char*>(reg->ecx);
    uint32_t size       = reg->edx;
    sockaddr* peeraddr  = reinterpret_cast<sockaddr*>(reg->esi);
    return v_recvfrom(current_pcb(), fd, buffer, size, peeraddr);
}

// CONNECT(ebx = fd, ecx = addr, edx = port)  → returns 0 if succeeded
int sys_connect(interrupt_frame* reg) {
    int fd           = static_cast<int>(reg->ebx);
    const char* addr = reinterpret_cast<char*>(reg->ecx);
    uint16_t port    = reg->edx;
    return v_connect(current_pcb(), fd, addr, port);
}

// IOCTL(ebx = fd, ecx = cmd, edx = arg)  → returns 0 if succeeded
int sys_ioctl(interrupt_frame* reg) {
    int fd          = static_cast<int>(reg->ebx);
    const char* cmd = reinterpret_cast<char*>(reg->ecx);
    void* arg       = reinterpret_cast<void*>(reg->edx);
    return v_ioctl(current_pcb(), fd, cmd, arg);
}

// STAT(ebx = path, ecx = &file_stat)
int sys_stat(interrupt_frame* reg) {
    const char* path = reinterpret_cast<const char*>(reg->ebx);
    file_stat*  out  = reinterpret_cast<file_stat*>(reg->ecx);
    return v_stat(path, out);
}

// MOUNT(ebx = driver_id, ecx = mount_path, edx = device_data)
int sys_mount(interrupt_frame* reg) {
    FS_DRIVER driver  = static_cast<FS_DRIVER>(reg->ebx);
    const char* path  = reinterpret_cast<const char*>(reg->ecx);
    void* device_data = reinterpret_cast<void*>(reg->edx);
    return v_mount(driver, path, device_data) == nullptr ? -1 : 0;
}

// UNMOUNT(ebx = mount_path)
int sys_unmount(interrupt_frame* reg) {
    const char* path = reinterpret_cast<const char*>(reg->ebx);
    return v_unmount(path);
}

// OPEN(ebx = path, ecx = mode)  → returns fd
int sys_open(interrupt_frame* reg) {
    SpinlockGuard guard(process_list_lock);
    const char* path = reinterpret_cast<const char*>(reg->ebx);
    uint8_t mode     = static_cast<uint8_t>(reg->ecx);
    return v_open(current_pcb(), path, mode);
}

// READ(ebx = fd, ecx = buffer, edx = size)  → returns bytes read
int sys_read(interrupt_frame* reg) {
    int fd         = static_cast<int>(reg->ebx);
    char* buffer   = reinterpret_cast<char*>(reg->ecx);
    uint32_t size  = reg->edx;
    return v_read(current_pcb(), fd, buffer, size);
}

// WRITE(ebx = fd, ecx = buffer, edx = size)  → returns bytes written
int sys_write(interrupt_frame* reg) {
    int fd              = static_cast<int>(reg->ebx);
    const char* buffer  = reinterpret_cast<const char*>(reg->ecx);
    uint32_t size       = reg->edx;
    return v_write(current_pcb(), fd, buffer, size);
}

// CLOSE(ebx = fd)
int sys_close(interrupt_frame* reg) {
    SpinlockGuard guard(process_list_lock);
    int fd = static_cast<int>(reg->ebx);
    return v_close(current_pcb(), fd);
}

// OPENDIR(ebx = path)  → returns dir fd
int sys_opendir(interrupt_frame* reg) {
    SpinlockGuard guard(process_list_lock);
    const char* path = reinterpret_cast<const char*>(reg->ebx);
    return v_opendir(current_pcb(), path);
}

// READDIR(ebx = fd, ecx = &dirent)  → 0 on success, -1 on end/error
int sys_readdir(interrupt_frame* reg) {
    SpinlockGuard guard(process_list_lock);
    int fd        = static_cast<int>(reg->ebx);
    dirent* out   = reinterpret_cast<dirent*>(reg->ecx);
    return v_readdir(current_pcb(), fd, out);
}

// CLOSEDIR(ebx = fd)
int sys_closedir(interrupt_frame* reg) {
    SpinlockGuard guard(process_list_lock);
    int fd = static_cast<int>(reg->ebx);
    return v_closedir(current_pcb(), fd);
}

// SYS_CHDIR(ebx = path)  → 0 on success, -1 on end/error
int sys_chdir(interrupt_frame* reg) {
    SpinlockGuard guard(process_list_lock);
    const char* path = reinterpret_cast<const char*>(reg->ebx);
    PCB* cur_pcb = current_pcb();
    
    char resolved[MAX_PATH_LEN];
    resolve_path(cur_pcb->cwd, path, resolved);
    file_stat st;
    if (v_stat(resolved, &st) != 0) return -1;
    if (st.type != 0) return -1;
    
    strcpy(cur_pcb->cwd, resolved);
    return 0;
}

// GETCWD(ebx = buf, ecx = size) → returns 0 on success, -1 on failure
int sys_getcwd(interrupt_frame* reg) {
    SpinlockGuard guard(process_list_lock);
    char* buf       = reinterpret_cast<char*>(reg->ebx);
    uint32_t size   = static_cast<uint32_t>(reg->ecx);
    PCB* proc = current_pcb();

    uint32_t len = strlen(proc->cwd) + 1;
    if (len > size) return -1;

    strcpy(buf, proc->cwd);
    return 0;
}

// EXEC(ebx = code, ecx = code_size, edx = argc, esi = argv, ebp = remaps, edi = remap_cnt)
int sys_exec(interrupt_frame* reg) {
    void*      code      = reinterpret_cast<void*>(reg->ebx);
    uint32_t   code_size = reg->ecx;
    int        argc      = static_cast<int>(reg->edx);
    char**     argv      = reinterpret_cast<char**>(reg->esi);
    fd_remap*  remaps    = reinterpret_cast<fd_remap*>(reg->ebp);
    int        remap_cnt = int(reg->edi);
    char name[64];
    strcpy(name, argv[0]);
    return static_cast<int>(exec(name, code, code_size, 4, argc, argv, remaps, remap_cnt));
}

// WAITPID(ebx = pid)
int sys_waitpid(interrupt_frame* reg) {
    pid_t pid = static_cast<pid_t>(reg->ebx);
    return waitpid(pid);
}

// SBRK(ebx = increment)
int sys_sbrk(interrupt_frame* reg) {
    SpinlockGuard guard(process_list_lock);
    uintptr_t increment = static_cast<uintptr_t>(reg->ebx);
    PCB* cur_pcb = current_pcb();
    uintptr_t old_break = cur_pcb->heap_break;
    uintptr_t new_break = old_break + increment;

    if (new_break < cur_pcb->heap_start) return -1;

    if (increment > 0) {
        uintptr_t old_page = (old_break + 0xFFF) & ~0xFFF;
        uintptr_t new_page = (new_break + 0xFFF) & ~0xFFF; // 按页对齐
        for (uintptr_t addr = old_page; addr < new_page; addr += 0x1000) {
            if (vmm_get_mapping(addr) == 0) {
                uintptr_t phys = reinterpret_cast<uintptr_t>(pmm_alloc(1 << 12));
                vmm_map_page(phys, addr, 0x7); // Present | RW | User
                memset((void*)addr, 0, 0x1000);
            }
        }
    }

    cur_pcb->heap_break = new_break;
    return old_break;
}

// PIPE(ebx = fds[2])
int sys_pipe(interrupt_frame* reg) {
    int* fds = reinterpret_cast<int*>(reg->ebx);
    return kpipe(fds);
}

// SLEEP(ebx = ms)
int sys_sleep(interrupt_frame* reg) {
    uint32_t fds = reinterpret_cast<uint32_t>(reg->ebx);
    sleep(fds * 1000);
    return 0;
}

// POLL(ebx = ms)
// 仅限字节流描述符使用！如stdin，tcp_socket
int sys_poll(interrupt_frame* reg) {
    pollfd* fds         = reinterpret_cast<pollfd*>(reg->ebx);
    uint32_t fd_num     = reg->ecx;
    uint32_t timeout    = reg->edx;
    process_queue poll_queue = nullptr;
    bool has_event = false;
    bool has_data = false;
    for (uint32_t i = 0; i < fd_num; ++i) {
        fds[i].revents = 0;
    }
    {
        SpinlockGuard guard(process_list_lock);
        PCB* cur_pcb = current_pcb();
        insert_into_waiting_queue(poll_queue, cur_pcb);
        for (int i = 0; i < fd_num; ++i) {
            if (cur_pcb->fd[fds[i].fd] == nullptr) {
                if (fds[i].events & INVFD) {
                    fds[i].revents |= INVFD;
                    has_event = true;
                }
            } else {
                int ret = v_peek(cur_pcb, fds[i].fd);
                if (ret < 0 && (fds[i].events & ERROR)) {
                    fds[i].revents |= ERROR;
                    has_event = true;
                } else if (ret > 0 && (fds[i].events & POLLIN)) {
                    fds[i].revents |= POLLIN;
                    has_event = true;
                    has_data = true;
                }
                v_setpoll(cur_pcb, fds[i].fd, &poll_queue);
            }
        }
        if (!has_event) {
            current_pcb()->state = process_state::WAITING;
        }
    }
    if (!has_event) {
        ::timeout(&poll_queue, timeout);
    } else {
        SpinlockGuard guard(process_list_lock);
        remove_from_waiting_queue(poll_queue, current_pcb()->pid);
        for (int i = 0; i < fd_num; ++i) {
            if (current_pcb()->fd[fds[i].fd] != nullptr) {
                v_setpoll(current_pcb(), fds[i].fd, nullptr);
            }
        }
        return 1;
    }
    {
        SpinlockGuard guard(process_list_lock);
        PCB* cur_pcb = current_pcb();
        for (int i = 0; i < fd_num; ++i) {
            if (cur_pcb->fd[fds[i].fd] == nullptr) {
                if (fds[i].events & INVFD) {
                    fds[i].revents |= INVFD;
                }
            } else {
                int ret = v_peek(cur_pcb, fds[i].fd);
                if (ret < 0 && (fds[i].events & ERROR)) {
                    fds[i].revents |= ERROR;
                } else if (ret > 0 && (fds[i].events & POLLIN)) {
                    fds[i].revents |= POLLIN;
                    has_data = true;
                }
                v_setpoll(cur_pcb, fds[i].fd, nullptr);
            }
        }
    }
    return has_data;
}

// CLOCK(eax = ticks)
int sys_clock(interrupt_frame* reg) {
    return pit_get_ticks();
}

// GETPID(eax = cur_pid)
int sys_getpid(interrupt_frame* reg) {
    return cur_process_id;
}

// SETPGID(ebx = child_pid, ecx = parent_pid) → returns 0 on success, -1 on failure
int sys_setpgid(interrupt_frame* reg) {
    // pid_t child_pid     = static_cast<pid_t>(reg->ecx);
    // pid_t parent_pid    = static_cast<pid_t>(reg->ecx);
    // todo：我们还没有进程组这个概念
    return 0;
}

// TCSETGRP(ebx = fd, ecx = pid) → returns 0 on success, -1 on failure
int sys_tcsetpgrp(interrupt_frame* reg) {
    // int fd       = static_cast<int>(reg->ebx);
    pid_t pid    = static_cast<pid_t>(reg->ecx);
    // todo：这里先设置唯一的一个tty
    terminal_setforeground(pid);
    return 0;
}

void syscall_init() {
    printf("syscall initializing...");
    register_syscall(uint32_t(SYSCALL::EXIT), sys_exit);
    register_syscall(uint32_t(SYSCALL::TERMINAL_WRITE), sys_terminal_write);
    register_syscall(uint32_t(SYSCALL::TERMINAL_SET_TEXT_COLOR), sys_terminal_set_text_color);
    register_syscall(uint32_t(SYSCALL::TERMINAL_GET_LINE), sys_terminal_getline);
    register_syscall(uint32_t(SYSCALL::TERMINAL_CLEAR), sys_terminal_clear);
    register_syscall(uint32_t(SYSCALL::TCSETPGRP),  sys_tcsetpgrp);
    register_syscall(uint32_t(SYSCALL::CLOCK), sys_clock);
    register_syscall(uint32_t(SYSCALL::SBRK),     sys_sbrk);
    register_syscall(uint32_t(SYSCALL::CONNECT),  sys_connect);
    register_syscall(uint32_t(SYSCALL::LISTEN),  sys_listen);
    register_syscall(uint32_t(SYSCALL::ACCEPT),  sys_accept);
    register_syscall(uint32_t(SYSCALL::SENDTO),  sys_sendto);
    register_syscall(uint32_t(SYSCALL::RECVFROM),  sys_recvfrom);
    register_syscall(uint32_t(SYSCALL::IOCTL),  sys_ioctl);
    register_syscall(uint32_t(SYSCALL::STAT),     sys_stat);
    register_syscall(uint32_t(SYSCALL::MOUNT),    sys_mount);
    register_syscall(uint32_t(SYSCALL::UNMOUNT),  sys_unmount);
    register_syscall(uint32_t(SYSCALL::OPEN),     sys_open);
    register_syscall(uint32_t(SYSCALL::READ),     sys_read);
    register_syscall(uint32_t(SYSCALL::WRITE),    sys_write);
    register_syscall(uint32_t(SYSCALL::CLOSE),    sys_close);
    register_syscall(uint32_t(SYSCALL::OPENDIR),  sys_opendir);
    register_syscall(uint32_t(SYSCALL::READDIR),  sys_readdir);
    register_syscall(uint32_t(SYSCALL::CLOSEDIR), sys_closedir);
    register_syscall(uint32_t(SYSCALL::CHDIR),    sys_chdir);
    register_syscall(uint32_t(SYSCALL::GETCWD),   sys_getcwd);
    register_syscall(uint32_t(SYSCALL::PIPE),  sys_pipe);
    register_syscall(uint32_t(SYSCALL::EXEC),     sys_exec);
    register_syscall(uint32_t(SYSCALL::WAITPID),  sys_waitpid);
    register_syscall(uint32_t(SYSCALL::SLEEP),  sys_sleep);
    register_syscall(uint32_t(SYSCALL::POLL),  sys_poll);
    register_syscall(uint32_t(SYSCALL::SETPGID),  sys_setpgid);
    register_syscall(uint32_t(SYSCALL::GETPID),  sys_getpid);
    printf("OK\n");
}

void register_syscall(uint8_t n, syscall_handler_t handler) {
    syscall_table[n] = handler;
}

void inner_syscall_handler(interrupt_frame* reg) {
    uint32_t syscall_num = reg->eax;
    if (syscall_num >= MAX_SYSCALL || !syscall_table[syscall_num]) {
        reg->eax = (int)(SYSCALL_RET::SYSCALL_NOT_FOUND);
        return;
    }
    reg->eax = (syscall_table[syscall_num])(reg);
}