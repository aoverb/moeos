#include <driver/block.hpp>
#include <kernel/mm.hpp>
#include <driver/ext2.hpp>
block_device* bdev_list[MAX_BLOCK_DEVICE_NUM];
size_t bdev_cnt = 0;

constexpr uint32_t SUPERBLOCK_SIZE = 1024;

void block_init() {
    for (int i = 0; i < bdev_cnt; ++i) {
        block_device* bd = bdev_list[i];

        // 这里直接先当成EXT2处理，其实这是不对的...如果后面要支持FAT，要先判断是不是FAT
        bd->block_size = SUPERBLOCK_SIZE; // 我们先把块大小调为1024，读取超级块的内容
        void* buffer = kmalloc(SUPERBLOCK_SIZE);
        bd->read(bd, 1, buffer);
        // 不是EXT2，直接放弃初始化
        ext2_super_block* ext2_sb = reinterpret_cast<ext2_super_block*>(buffer);
        if (ext2_sb->s_magic != 0xEF53) {
            bd->fs = file_system::UNKNOWN; // 用这个来标记未知的块设备
            kfree(buffer);
            continue;
        }
        bd->block_size = (1 << ext2_sb->s_log_block_size) * 1024;
        bd->fs = file_system::EXT2;
    }
}

int register_block_device(block_device* bd) {
    if (bdev_cnt >= MAX_BLOCK_DEVICE_NUM) return -1;
    bdev_list[bdev_cnt] = bd;
    return bdev_cnt++;
}