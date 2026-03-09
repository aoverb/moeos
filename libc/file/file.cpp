#include <file.h>
#include <syscall_def.hpp>

int ioctl(int fd, char* cmd, void* arg) {
    return syscall3((uint32_t)SYSCALL::IOCTL, (uint32_t)fd, (uint32_t)cmd, (uint32_t)arg);
}

int stat(const char* path, file_stat* stat) {
    return syscall2((uint32_t)SYSCALL::STAT, (uint32_t)path, (uint32_t)stat);
}

int mount(uint32_t driver, const char* mount_path, void* device_data) {
    return syscall3((uint32_t)SYSCALL::MOUNT, driver, (uint32_t)mount_path, (uint32_t)device_data);
}

int unmount(const char* mount_path) {
    return syscall1((uint32_t)SYSCALL::UNMOUNT, (uint32_t)mount_path);
}

int open(const char* path, uint8_t mode) {
    return syscall2((uint32_t)SYSCALL::OPEN, (uint32_t)path, (uint32_t)mode);
}

int read(int fd, char* buffer, uint32_t size) {
    return syscall3((uint32_t)SYSCALL::READ, (uint32_t)fd, (uint32_t)buffer, size);
}

int write(int fd, const char* buffer, uint32_t size) {
    return syscall3((uint32_t)SYSCALL::WRITE, (uint32_t)fd, (uint32_t)buffer, size);
}

int close(int fd) {
    return syscall1((uint32_t)SYSCALL::CLOSE, (uint32_t)fd);
}

int opendir(const char* path) {
    return syscall1((uint32_t)SYSCALL::OPENDIR, (uint32_t)path);
}

int readdir(int fd, dirent* out) {
    return syscall2((uint32_t)SYSCALL::READDIR, (uint32_t)fd, (uint32_t)out);
}

int getcwd(char* buf, uint32_t size) {
    return syscall2((uint32_t)SYSCALL::GETCWD, (uint32_t)buf, (uint32_t)size);
}

int closedir(int fd) {
    return syscall1((uint32_t)SYSCALL::CLOSEDIR, (uint32_t)fd);
}

int chdir(const char* path) {
    return syscall1((uint32_t)SYSCALL::CHDIR, (uint32_t)path);
}

int exec(void* code, uint32_t code_size, int argc, char** argv, fd_remap* remaps, int remap_cnt) {
    return (uint32_t)syscall6((uint32_t)SYSCALL::EXEC,
                              (uint32_t)code,
                              code_size,
                              (uint32_t)argc,
                              (uint32_t)argv,
                              (uint32_t)remaps,
                              (uint32_t)remap_cnt);
}