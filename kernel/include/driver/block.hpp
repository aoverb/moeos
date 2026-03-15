#ifndef _DRIVER_BLOCK_H
#define _DRIVER_BLOCK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum class file_system { EXT2, UNKNOWN };

struct block_device {
    uint8_t device_id;
    uint32_t block_size;
    uint32_t sector_size;
    uint32_t total_sectors;
    file_system fs;
    void* data;

    int (*read)(block_device* dev, uint32_t block_no, void* buffer);
    int (*write)(block_device* dev, uint32_t block_no, const void* buffer);
};

constexpr size_t MAX_BLOCK_DEVICE_NUM = 256;
extern block_device* bdev_list[MAX_BLOCK_DEVICE_NUM];
extern size_t bdev_cnt;

int register_block_device(block_device* bd);
void block_init();

#ifdef __cplusplus
}
#endif

#endif