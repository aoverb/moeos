#ifndef _DRIVER_TARFS_H
#define _DRIVER_TARFS_H

#include <stdint.h>
#include <string>
#include <unordered_map>

#ifdef __cplusplus
extern "C" {
#endif

// tarfs.hpp
// 我是驱动，我负责的事情：
// mount时，给我一块内存区域和对应的大小，我会去检查记录的正确性，但是我不会把状态记录在驱动层，驱动是无状态的
// 我会把mount传入的mount_data重转换成tarfs_data，里面有一个session数组，我去把它给清空掉
// open时，我会根据传入的mounting_point里面找到tarfs_data，看看有没有对应的文件，有的话我就去session数组增加一个session
// 并把这个session相关的信息记录下来，传回这个session_id

void init_tarfs();
struct file_handle {
    uint32_t inode_no;
    uint32_t offset;
    uint32_t mode;
    uint8_t type;
    uint8_t valid;
};

using inode_id = uint32_t;

struct tarfs_metadata {
    void* data;
    uint32_t size;
};

#ifdef __cplusplus
}
#endif

#endif