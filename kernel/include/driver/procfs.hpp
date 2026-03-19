#ifndef _DRIVER_PROCFS_H
#define _DRIVER_PROCFS_H

#include <stdint.h>
#include <driver/vfs.hpp>

#ifdef __cplusplus
extern "C" {
#endif

struct proc_operation {
    int (*read)(char* buffer, uint32_t offset, uint32_t size);
    int (*write)(const char* buffer, uint32_t size);
    int (*ioctl)(uint32_t request, void* arg);
};

void init_procfs();
int register_info_in_procfs(mounting_point* mp, const char* path, const char* info_name,
    proc_operation* opr, bool is_dir);

#ifdef __cplusplus
}
#endif

#endif