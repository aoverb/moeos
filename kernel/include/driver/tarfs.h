#ifndef _DRIVER_TARFS_H
#define _DRIVER_TARFS_H

#include <stdint.h>
#include <string>
#include <unordered_map>

#ifdef __cplusplus
extern "C" {
#endif

// tarfs.h
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

constexpr uint32_t READ = 1;
constexpr uint32_t MAX_INODE_NUM = 32768;
constexpr uint32_t MAX_HANDLE_NUM = 4096;

typedef struct {
    char name[100];
    char filemode[8];
    char owner_id[8];
    char group_id[8];
    char size[12];
    char last_modified[12];
    char checksum[8];
    char type;
    char name_of_link[100];
    char ustar_indicator[6];
    char ustar_ver[2];
    char owner_name[32];
    char group_name[32];
    char dev_major_no[8];
    char dev_minor_no[8];
    char name_prefix[155];
    char padding[12];
} tar_block;
static_assert(sizeof(tar_block) == 512, "tar_block must be 512 bytes!");

using inode_id = uint32_t;
struct tar_inode {
    tar_block* block = nullptr;
    std::unordered_map<std::string, inode_id> child_inodes;
};

struct tarfs_data {
    void* tar_addr;
    uint32_t tar_size;
    tar_inode* inodes[MAX_INODE_NUM]; 
    uint32_t inode_cnt;
    file_handle file_handles[MAX_HANDLE_NUM];
};

struct tarfs_metadata {
    void* data;
    uint32_t size;
};

#ifdef __cplusplus
}
#endif

#endif