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

/* open() 文件创建标志 */
#define O_CREAT      0x0040   /* 文件不存在则创建 */
#define O_EXCL       0x0080   /* 与 O_CREAT 一起用，文件已存在则失败 */
#define O_TRUNC      0x0200   /* 截断为零长度 */
#define O_APPEND     0x0400   /* 追加模式 */
 
/* open() 非阻塞 */
#define O_NONBLOCK   0x0800
 
/* 文件权限位（mode_t，用于 open 第三个参数） */
#define S_IRUSR      0400
#define S_IWUSR      0200
#define S_IRGRP      0040
#define S_IROTH      0004

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

int stat(const char* path, file_stat* stat);
int mount(uint32_t driver, const char* mount_path, void* device_data);
int unmount(const char* mount_path);
int open(const char* path, uint8_t mode);
int read(int fd, char* buffer, uint32_t size);
int write(int fd, const char* buffer, uint32_t size);
int close(int fd);
int exec(void* code, uint32_t code_size, int argc, char** argv,
    fd_remap* remaps = nullptr, int remap_cnt = 0);
int unlink(const char* path);

typedef struct {
    char     name[256];
    uint32_t inode;
    uint8_t  type;
} dirent;

int opendir(const char* path);
int readdir(int fd, dirent* out);
int closedir(int fd);
int mkdir(const char* path);
int chdir(const char* path);
int getcwd(char* buf, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
