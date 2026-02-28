#include <kernel/syscall.h>
#include <syscall_def.h>
#include <kernel/process.h>
#include <driver/vfs.h>
#include <kernel/tty.h>
#include <string.h>
#include <stdio.h>

constexpr uint32_t MAX_SYSCALL = 255;
syscall_handler_t syscall_table[255] = { nullptr };

int sys_exit(interrupt_frame*) {
    exit_process(cur_process_id);
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

// EXEC(ebx = code_ptr, ecx = code_size, edx = priority) → returns pid
int sys_exec(interrupt_frame* reg) {
    void*    code      = reinterpret_cast<void*>(reg->ebx);
    uint32_t code_size = reg->ecx;
    uint8_t  priority  = static_cast<uint8_t>(reg->edx);
    return static_cast<int>(create_user_process(code, code_size, priority));
}

void syscall_init() {
    register_syscall(uint32_t(SYSCALL::EXIT), sys_exit);
    register_syscall(uint32_t(SYSCALL::TERMINAL_WRITE), sys_terminal_write);
    register_syscall(uint32_t(SYSCALL::TERMINAL_SET_TEXT_COLOR), sys_terminal_set_text_color);
    register_syscall(uint32_t(SYSCALL::TERMINAL_GET_LINE), sys_terminal_getline);
    register_syscall(uint32_t(SYSCALL::MOUNT),    sys_mount);
    register_syscall(uint32_t(SYSCALL::UNMOUNT),  sys_unmount);
    register_syscall(uint32_t(SYSCALL::OPEN),     sys_open);
    register_syscall(uint32_t(SYSCALL::READ),     sys_read);
    register_syscall(uint32_t(SYSCALL::WRITE),    sys_write);
    register_syscall(uint32_t(SYSCALL::CLOSE),    sys_close);
    register_syscall(uint32_t(SYSCALL::OPENDIR),  sys_opendir);
    register_syscall(uint32_t(SYSCALL::READDIR),  sys_readdir);
    register_syscall(uint32_t(SYSCALL::CLOSEDIR), sys_closedir);
    register_syscall(uint32_t(SYSCALL::EXEC),     sys_exec);
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