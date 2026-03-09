#ifndef _FILE_H
#define _FILE_H 1
#include <stdint.h>
#include <pipe.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif

#define O_RDONLY  0x01
#define O_WRONLY  0x02
#define O_RDWR    0x03
#define O_CREATE  0x04

struct file_stat {
    uint32_t size;
    uint8_t  type;
    uint32_t mode;
    uint32_t owner_id;
    uint32_t group_id;
    uint32_t last_modified;
    char     owner_name[32];
    char     group_name[32];
    char     name[100];
    char     link_name[100];
};

int ioctl(int fd, char* cmd, void* arg);
int stat(const char* path, file_stat* stat);
int mount(uint32_t driver, const char* mount_path, void* device_data);
int unmount(const char* mount_path);
int open(const char* path, uint8_t mode);
int read(int fd, char* buffer, uint32_t size);
int write(int fd, const char* buffer, uint32_t size);
int close(int fd);
int exec(void* code, uint32_t code_size, int argc, char** argv,
    fd_remap* remaps = nullptr, int remap_cnt = 0);

typedef struct {
    char     name[256];
    uint32_t inode;
    uint8_t  type;
} dirent;

int opendir(const char* path);
int readdir(int fd, dirent* out);
int closedir(int fd);
int chdir(const char* path);
int getcwd(char* buf, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
