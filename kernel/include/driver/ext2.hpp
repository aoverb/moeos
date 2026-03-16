#ifndef _DRIVER_EXT2_H
#define _DRIVER_EXT2_H

#include <stdint.h>
#include <driver/vfs.hpp>
#include <driver/block.hpp>
#ifdef __cplusplus
extern "C" {
#endif

struct ext2_inode {
    uint16_t  i_mode;
    uint16_t  i_uid;
    uint32_t  i_size;
    uint32_t  i_atime;
    uint32_t  i_ctime;
    uint32_t  i_mtime;
    uint32_t  i_dtime;
    uint16_t  i_gid;
    uint16_t  i_links_count;
    uint32_t  i_blocks;
    uint32_t  i_flags;
    uint32_t  i_reserved1;        /* 原 osd1，保留占位 */
    uint32_t  i_block[15];
    uint32_t  i_generation;
    uint32_t  i_file_acl;
    uint32_t  i_dir_acl;          /* 对普通文件作为 i_size_high */
    uint32_t  i_faddr;
    /* 原 osd2，展开为 Linux 变体 */
    uint8_t   i_frag;
    uint8_t   i_fsize;
    uint16_t  i_pad1;
    uint16_t  i_uid_high;         /* UID 高 16 位 */
    uint16_t  i_gid_high;         /* GID 高 16 位 */
    uint32_t  i_reserved2;
};

struct ext2_group_desc {
    uint32_t bg_block_bitmap;       // 块位图所在的块号
    uint32_t bg_inode_bitmap;       // inode 位图所在的块号
    uint32_t bg_inode_table;        // inode 表起始块号
    uint16_t bg_free_blocks_count;  // 本组空闲块数
    uint16_t bg_free_inodes_count;  // 本组空闲 inode 数
    uint16_t bg_used_dirs_count;    // 本组目录数
    uint16_t bg_pad;                // 对齐填充
    uint32_t bg_reserved[3];        // 保留字段
};

struct ext2_super_block {
    uint32_t s_inodes_count;        /* Inode 总数 */
    uint32_t s_blocks_count;        /* 块总数 */
    uint32_t s_r_blocks_count;      /* 保留块数 */
    uint32_t s_free_blocks_count;   /* 空闲块数 */
    uint32_t s_free_inodes_count;   /* 空闲 Inode 数 */
    uint32_t s_first_data_block;    /* 第一个数据块号 */
    uint32_t s_log_block_size;      /* 块大小 (log2) */
    uint32_t s_log_frag_size;       /* 片大小 (log2) */
    uint32_t s_blocks_per_group;    /* 每组块数 */
    uint32_t s_frags_per_group;     /* 每组片数 */
    uint32_t s_inodes_per_group;    /* 每组 Inode 数 */
    uint32_t s_mtime;               /* 最后挂载时间 */
    uint32_t s_wtime;               /* 最后写入时间 */
    uint16_t s_mnt_count;           /* 挂载次数 */
    uint16_t s_max_mnt_count;       /* 最大挂载数 */
    uint16_t s_magic;               /* 魔数 0xEF53 */
    uint16_t s_state;               /* 文件系统状态 */
    uint16_t s_errors;              /* 错误行为 */
    uint16_t s_minor_rev_level;     /* 次版本号 */
    uint32_t s_lastcheck;           /* 最后检查时间 */
    uint32_t s_checkinterval;       /* 检查间隔 */
    uint32_t s_creator_os;          /* 创建系统 */
    uint32_t s_rev_level;           /* 主版本号 */
    uint16_t s_def_resuid;          /* 保留 UID */
    uint16_t s_def_resgid;          /* 保留 GID */
    uint32_t s_first_ino;           /* offset 84: 第一个非保留 inode */
    uint16_t s_inode_size;          /* offset 88: 磁盘上 inode 大小 */
};

struct cache_data;

struct ext2_data {
    block_device* dev;
    ext2_super_block sb;

    ext2_group_desc* gdt;
    uint32_t bg_num;

    ext2_inode root_inode;
    uint32_t block_num[4]; // 记录每一级指针有多少个数据块，0级是直接指针
    cache_data* cache_data;
};

void init_ext2fs();
int ext2_read(mounting_point* mp, uint32_t inode_id, uint32_t offset, char* buffer, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif