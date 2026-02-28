#ifndef _DRIVER_VFS_H
#define _DRIVER_VFS_H

#include <stdint.h>
#include <kernel/process.h>

#ifdef __cplusplus
extern "C" {
#endif

constexpr uint32_t MAX_DRIVER_NUM = 32;
constexpr uint32_t MAX_PATH_LEN = 256;
enum class FS_DRIVER {
    TARFS = 0
};

struct dirent {
    char name[256];
    uint32_t inode;
    char type;
};

struct file_stat {
    uint32_t size;
    uint8_t type;
    uint32_t mode;
};

struct fs_operation {
    int (*mount)(mounting_point* mp);
    int (*unmount)(mounting_point* mp);
    int (*open)(mounting_point* mp, const char* path, uint8_t mode);
    int (*close)(mounting_point* mp, uint32_t handle_id);
    int (*read)(mounting_point* mp, uint32_t handle_id, char* buffer, uint32_t size);
    int (*write)(mounting_point* mp, uint32_t handle_id, const char* buffer, uint32_t size);
    int (*opendir)(mounting_point* mp, const char* path);
    int (*readdir)(mounting_point* mp, uint32_t handle_id, dirent* out);
    int (*closedir)(mounting_point* mp, uint32_t handle_id);
    int (*stat)(mounting_point* mp, const char* path, file_stat* out);
};

typedef struct mounting_point {
	uint32_t index;
    FS_DRIVER driver;
    char mount_path[MAX_PATH_LEN];
    fs_operation* operations;
    void* data;
} mounting_point;

void init_vfs();

int register_fs_operation(FS_DRIVER driver, fs_operation* operations);
int v_mount(FS_DRIVER driver, const char* mount_path, void* device_data);
int v_unmount(const char* mount_path);
int v_open(PCB* proc, const char* path, uint8_t mode);
int v_read(PCB* proc, int fd, char* buffer, uint32_t size);
int v_write(PCB* proc, int fd, const char* buffer, uint32_t size);
int v_close(PCB* proc, int fd);
int v_opendir(PCB* proc, const char* path);
int v_readdir(PCB* proc, int fd, dirent* out);
int v_closedir(PCB* proc, int fd);
int v_stat(const char* path, file_stat* out);

#ifdef __cplusplus
}
#endif

#endif