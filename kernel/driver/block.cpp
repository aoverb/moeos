#include <driver/block.hpp>

constexpr size_t MAX_BLOCK_DEVICE_NUM = 256;
static block_device* bdev_list[MAX_BLOCK_DEVICE_NUM];
static size_t bdev_cnt = 0;

int register_block_device(block_device* bd) {
    if (bdev_cnt >= MAX_BLOCK_DEVICE_NUM) return -1;
    bdev_list[++bdev_cnt] = bd;
    return bdev_cnt;
}