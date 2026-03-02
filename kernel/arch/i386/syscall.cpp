#include <kernel/syscall.h>
#include <kernel/mm.h>
#include <syscall_def.h>
#include <kernel/process.h>
#include <driver/vfs.h>
#include <kernel/tty.h>
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

int sys_terminal_getline(interrupt_frame* reg) {
    getline(reinterpret_cast<char*>(reg->ebx), reg->ecx);
    return 0;
}

static PCB* current_pcb() {
    return process_list[cur_process_id];
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
    return v_mount(driver, path, device_data);
}

// UNMOUNT(ebx = mount_path)
int sys_unmount(interrupt_frame* reg) {
    const char* path = reinterpret_cast<const char*>(reg->ebx);
    return v_unmount(path);
}

// OPEN(ebx = path, ecx = mode)  → returns fd
int sys_open(interrupt_frame* reg) {
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
    int fd = static_cast<int>(reg->ebx);
    return v_close(current_pcb(), fd);
}

// OPENDIR(ebx = path)  → returns dir fd
int sys_opendir(interrupt_frame* reg) {
    const char* path = reinterpret_cast<const char*>(reg->ebx);
    return v_opendir(current_pcb(), path);
}

// READDIR(ebx = fd, ecx = &dirent)  → 0 on success, -1 on end/error
int sys_readdir(interrupt_frame* reg) {
    int fd        = static_cast<int>(reg->ebx);
    dirent* out   = reinterpret_cast<dirent*>(reg->ecx);
    return v_readdir(current_pcb(), fd, out);
}

// CLOSEDIR(ebx = fd)
int sys_closedir(interrupt_frame* reg) {
    int fd = static_cast<int>(reg->ebx);
    return v_closedir(current_pcb(), fd);
}

// SYS_CHDIR(ebx = path)  → 0 on success, -1 on end/error
int sys_chdir(interrupt_frame* reg) {
    const char* path = reinterpret_cast<const char*>(reg->ebx);
    PCB* cur_pcb = process_list[cur_process_id];
    
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
    char* buf       = reinterpret_cast<char*>(reg->ebx);
    uint32_t size   = static_cast<uint32_t>(reg->ecx);
    PCB* proc = current_pcb();

    uint32_t len = strlen(proc->cwd) + 1;
    if (len > size) return -1;

    strcpy(buf, proc->cwd);
    return 0;
}

// EXEC(ebx = code, ecx = code_size, edx = priority, esi = argc, ebp = argv)
int sys_exec(interrupt_frame* reg) {
    void*    code      = reinterpret_cast<void*>(reg->ebx);
    uint32_t code_size = reg->ecx;
    uint8_t  priority  = static_cast<uint8_t>(reg->edx);
    int      argc      = static_cast<int>(reg->esi);
    char**   argv      = reinterpret_cast<char**>(reg->ebp);
    return static_cast<int>(exec(code, code_size, priority, argc, argv));
}

// WAITPID(ebx = pid)
int sys_waitpid(interrupt_frame* reg) {
    pid_t pid = static_cast<pid_t>(reg->ebx);
    return waitpid(pid);
}

// SBRK(ebx = increment)
int sys_sbrk(interrupt_frame* reg) {
    uintptr_t increment = static_cast<uintptr_t>(reg->ebx);
    PCB* cur_pcb = process_list[cur_process_id];
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

void syscall_init() {
    register_syscall(uint32_t(SYSCALL::EXIT), sys_exit);
    register_syscall(uint32_t(SYSCALL::TERMINAL_WRITE), sys_terminal_write);
    register_syscall(uint32_t(SYSCALL::TERMINAL_SET_TEXT_COLOR), sys_terminal_set_text_color);
    register_syscall(uint32_t(SYSCALL::TERMINAL_GET_LINE), sys_terminal_getline);
    register_syscall(uint32_t(SYSCALL::SBRK),     sys_sbrk);
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
    register_syscall(uint32_t(SYSCALL::EXEC),     sys_exec);
    register_syscall(uint32_t(SYSCALL::WAITPID),  sys_waitpid);
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