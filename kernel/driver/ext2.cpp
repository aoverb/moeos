#include <driver/vfs.hpp>
#include <driver/ext2.hpp>
#include <stdio.h>

fs_operation ext2_fs_operation;

static int get_inode_by_id (ext2_data* data, uint32_t id, ext2_inode* out_inode) {
    if (id == 0) return -1;
    ext2_super_block& sb = data->sb;
    uint32_t inodes_per_group = sb.s_inodes_per_group;
    // 先算算我在哪个组
    uint32_t idx = id - 1;
    
    uint32_t group_idx = idx / inodes_per_group;
    
    ext2_group_desc& gd = data->gdt[group_idx];
    
    uint32_t inode_table_block_id = gd.bg_inode_table; // 这里拿到的是bg_inode_table开始的块号

    size_t inode_size = sizeof(ext2_inode);
    size_t block_size = data->dev->block_size;
    uint32_t inodes_count_in_each_block = block_size / inode_size;

    // 我们还得算一个相对的id，也就是在这个块组里面的id
    uint32_t idx_in_group = idx % inodes_per_group;

    uint32_t block_idx = idx_in_group / inodes_count_in_each_block;
    uint32_t offset = idx_in_group % inodes_count_in_each_block;

    void* buffer = kmalloc(block_size);
    data->dev->read(data->dev, inode_table_block_id + block_idx,  buffer);
    out_inode = (static_cast<ext2_inode*>(buffer) + offset);
    kfree(buffer);
    return 0;
}

static int mount(mounting_point* mp) {
    // 千里之行，始于足下...
    ext2_data* data = (ext2_data*)mp->data;

    // 读取超级块，超级块是在0号块之后偏移1024字节处
    uint32_t sb_block_num = 1024 / data->dev->block_size;
    uint32_t sb_offset = 1024 % data->dev->block_size;
    void* buffer = kmalloc(data->dev->block_size);
    int ret = data->dev->read(data->dev, sb_block_num, buffer);
    if (ret < 0) {
        kfree(buffer);
        return -1; // 读取超级块失败
    }
    ext2_super_block* ext2_sb = reinterpret_cast<ext2_super_block*>((char*)buffer + sb_offset);
    data->sb = *ext2_sb;
    kfree(buffer);

    ext2_super_block& sb = data->sb;

    // 检查魔数...
    if (sb.s_magic != 0xEF53) {
        return -1;
    }
    // 检查磁盘状态
    // 我们可以不做...
    // sb.s_state...

    // 缓存组描述符表
    data->bg_num = (sb.s_blocks_count + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;
    data->gdt = (ext2_group_desc*)kmalloc(sizeof(ext2_group_desc) * data->bg_num);

    // 读多少个块才能把所有的块组描述符读出来？
    uint32_t bg_block_num = (data->bg_num * sizeof(ext2_group_desc) + data->dev->block_size - 1) / data->dev->block_size;

    uint32_t gd_idx = 0;

    // 一个块最多可以读出 data->dev->block_size / sizeof(ext2_group_desc) 个组描述符
    uint32_t gd_per_block = (data->dev->block_size / sizeof(ext2_group_desc));
    for (int blk_idx = 0; blk_idx < bg_block_num; ++blk_idx) {
        void* gd_buffer = kmalloc(data->dev->block_size);
        data->dev->read(data->dev, sb_block_num + 1 + blk_idx, gd_buffer); // 组描述符表所在的块紧跟超级块所在的块
        
        for (int i = 0; i < gd_per_block; ++i) {
            data->gdt[gd_idx++] = ((ext2_group_desc*)gd_buffer)[i];
            if (gd_idx == data->bg_num) {
                break;
            }
        }

        kfree(gd_buffer);
    }

    get_inode_by_id(data, 2, &data->root_inode);
    return 0;
}

int stat(mounting_point* mp, const char* path, file_stat* out) {
    ext2_data* data = (ext2_data*)mp->data;
    if (strcmp(path, "/") == 0) {
        out->group_id = (data->root_inode.i_gid_high << 16) | data->root_inode.i_gid;
        out->size = data->root_inode.i_fsize;
        out->mode = data->root_inode.i_mode;
        out->last_modified = data->root_inode.i_mtime;
        out->type = 0;
        return 0;
    }
    return -1;
}

void init_ext2fs() {
    ext2_fs_operation.mount    = &mount;
    ext2_fs_operation.unmount  = nullptr;
    ext2_fs_operation.open     = nullptr;
    ext2_fs_operation.read     = nullptr;
    ext2_fs_operation.write    = nullptr;
    ext2_fs_operation.close    = nullptr;
    ext2_fs_operation.opendir  = nullptr;
    ext2_fs_operation.readdir  = nullptr;
    ext2_fs_operation.closedir = nullptr;
    ext2_fs_operation.stat     = &stat;
    ext2_fs_operation.ioctl    = nullptr;
    ext2_fs_operation.set_poll = nullptr;
    ext2_fs_operation.peek     = nullptr;
    ext2_fs_operation.sock_opr = nullptr;
    register_fs_operation(FS_DRIVER::EXT2FS, nullptr); // 先让ext2挂载失败
}