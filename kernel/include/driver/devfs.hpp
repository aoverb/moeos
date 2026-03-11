#ifndef _DRIVER_DEVFS_H
#define _DRIVER_DEVFS_H

#include <stdint.h>
#include <driver/vfs.hpp>

#ifdef __cplusplus
extern "C" {
#endif
struct dev_operation {
    int (*read)(char* buffer, uint32_t offset, uint32_t size);
    int (*write)(const char* buffer, uint32_t size);
    int (*set_poll)(process_queue* poll_queue);
    int (*peek)();
};

void init_devfs();
int register_in_devfs(mounting_point* mp, const char* dev_name, dev_operation* opr);

#ifdef __cplusplus
}
#endif

#endif