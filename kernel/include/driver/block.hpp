#ifndef _DRIVER_BLOCK_H
#define _DRIVER_BLOCK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct block_device {
    uint8_t device_id;
    uint32_t block_size;
    uint32_t sector_size;
    uint32_t total_sectors;
    void* data;

    int (*read)(block_device* dev, uint32_t block_no, void* buffer);
    int (*write)(block_device* dev, uint32_t block_no, const void* buffer);
};

int register_block_device(block_device* bd);

#ifdef __cplusplus
}
#endif

#endif