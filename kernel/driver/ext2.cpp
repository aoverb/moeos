#include <driver/vfs.hpp>
#include <driver/ext2.hpp>
#include <stdio.h>

fs_operation ext2_fs_operation;

void read_direct_block(ext2_data* data, const ext2_inode* inode, uint32_t block_idx,
    uint32_t block_count, char* buffer) {
    const size_t block_size = data->dev->block_size;
    for (int i = 0; i < block_count; ++i) {
        uint32_t blk = inode->i_block[block_idx + i];
        if (blk == 0) { // 空洞块，清零即可
            memset(buffer + i * block_size, 0, block_size);
        } else {
            data->dev->read(data->dev, blk, buffer + i * block_size);
        }
    }
    return;
}

void read_first_class_block(ext2_data* data, const ext2_inode* inode, uint32_t block_idx,
    uint32_t block_count, char* buffer) {
    const size_t block_size = data->dev->block_size;
    uint32_t blk = inode->i_block[12]; // 一级指针块
    if (blk == 0) {
        memset(buffer, 0, block_count * block_size);
        return;
    }
    uint32_t* buf = (uint32_t*)kmalloc(block_size);
    if (data->dev->read(data->dev, blk, buf) < 0) {
        // 记录错误
        kfree(buf);
        return;
    }
    for (int j = 0; j < block_count; ++j) { // 读出来的是直接指针
        uint32_t blk = buf[block_idx + j];
        if (blk == 0) {
            memset(buffer + j * block_size, 0, block_size);
        } else {
            data->dev->read(data->dev, blk, buffer + j * block_size);
        }
    }
    kfree(buf);
    return;
}

// 太复杂了..需要用递归重写
void read_second_class_block(ext2_data* data, const ext2_inode* inode, uint32_t block_idx,
    uint32_t block_count, char* buffer) {
    const size_t block_size = data->dev->block_size;
    uint32_t blk = inode->i_block[13]; // 二级指针块
    if (blk == 0) {
        memset(buffer, 0, block_count * block_size);
        return;
    }
    uint32_t* buf = (uint32_t*)kmalloc(block_size);
    if (data->dev->read(data->dev, blk, buf) < 0) {
        // 记录错误
        kfree(buf);
        return;
    }

    uint32_t first_index = (block_idx / data->blocks_in_first_class_pointer); // 先看下开头落在第几个一级指针
    uint32_t first_index_offset = block_idx % data->blocks_in_first_class_pointer; // 再看看在这个指针块的偏移是多少
    uint32_t first_left_blk = buf[first_index]; // 最左端的一级指针
    if (first_left_blk != 0) {
        uint32_t* direct_in_left_blk = (uint32_t*)kmalloc(block_size);
        if (data->dev->read(data->dev, first_left_blk, direct_in_left_blk) < 0) { // 读出最左端一级指针的所有直接指针
            // 记录错误
            kfree(buf);
            kfree(direct_in_left_blk);
            return;
        }

        for (int i = first_index_offset; i < data->blocks_in_first_class_pointer && block_count > 0; ++i) {
            uint32_t drt_buf = direct_in_left_blk[i]; // 直接指针指向的数据块号
            if (drt_buf == 0) {
                memset(buffer, 0, block_size);
            } else if (data->dev->read(data->dev, drt_buf, buffer) < 0) {
                // 记录错误
                kfree(buf);
                kfree(direct_in_left_blk);
                return;
            }
            buffer += block_size;
            --block_count;
            ++block_idx;
        }

        kfree(direct_in_left_blk);
    } else {
        memset(buffer, 0, (data->blocks_in_first_class_pointer - first_index_offset) * block_size);
        buffer += (data->blocks_in_first_class_pointer - first_index_offset) * block_size;
        block_count -= (data->blocks_in_first_class_pointer - first_index_offset);
        block_idx += (data->blocks_in_first_class_pointer - first_index_offset);
    }


    // 再看中间有几个可以完整读取的一级指针块
    uint32_t* mid_blk_buf = (uint32_t*)kmalloc(block_size);
    uint32_t mid_first_num = block_count / data->blocks_in_first_class_pointer;
    for (int i = 0; i < mid_first_num; ++i) {
        uint32_t mid_blk = buf[first_index + 1 + i]; // 最左端的一级指针往右数i + i个块
        if (mid_blk == 0) {
            memset(buffer, 0, data->blocks_in_first_class_pointer * block_size);
            buffer += data->blocks_in_first_class_pointer * block_size;
            continue;
        }
        
        if (data->dev->read(data->dev, mid_blk, mid_blk_buf) < 0) { // 读出当前一级指针块的所有直接指针
            kfree(mid_blk_buf);
            kfree(buf);
            return;
        }
        for (int j = 0; j < data->blocks_in_first_class_pointer; ++j) {
            uint32_t blk = mid_blk_buf[j];
            if (blk == 0) {
                memset(buffer, 0, block_size);
            } else {
                if (data->dev->read(data->dev, blk, buffer) < 0) {
                    kfree(mid_blk_buf);
                    kfree(buf);
                    return;
                }
            }
            buffer += block_size;
        }
        
    }
    kfree(mid_blk_buf);

    block_count -= data->blocks_in_first_class_pointer * mid_first_num;

    // 还有要读的话再看下最右边的
    if (block_count > 0) {
        uint32_t right_index = first_index + mid_first_num + 1;
        uint32_t right_blk = buf[right_index];
        if (right_blk == 0) {
            memset(buffer, 0, block_count * block_size);
            kfree(buf);
            return;
        }
        uint32_t* right_blk_buf = (uint32_t*)kmalloc(block_size);
        if (data->dev->read(data->dev, right_blk, right_blk_buf) < 0) { // 读直接指针
            // 记录错误
            kfree(right_blk_buf);
            kfree(buf);
            return;
        }
        for (int i = 0; i < block_count; ++i) {
            uint32_t blk = right_blk_buf[i];
            if (blk == 0) {
                memset(buffer, 0, block_size);
            } else {
                if (data->dev->read(data->dev, blk, buffer) < 0) {
                    kfree(right_blk_buf);
                    kfree(buf);
                    return;
                }
            }
            buffer += block_size;
        }
        kfree(right_blk_buf);
    }

    kfree(buf);
    return;
}

void read_third_class_block(ext2_data* data, const ext2_inode* inode, uint32_t block_idx,
    uint32_t block_count, char* buffer) {
        return; // todo: 太复杂了！
    }

static size_t read_block_in_inode(ext2_data* data, const ext2_inode* inode,
    uint32_t block_idx, uint32_t block_count, char* buffer) {
    uint32_t total_read_count = 0;
    const size_t block_size = data->dev->block_size; 
    if (block_idx < 12) {
        uint32_t read_count = (12 - block_idx) < block_count ? (12 - block_idx) : block_count;
        read_direct_block(data, inode, block_idx, read_count, buffer);
        buffer += read_count * block_size;
        block_count -= read_count;
        total_read_count += read_count;
        block_idx = 0;
    } else {
        block_idx -= 12; // 这里重置block_idx，是为了让它成为下面判断的每一级指针的相对数据块
    }
    if (block_count == 0) return total_read_count; // 提高效率

    if (block_idx < data->blocks_in_first_class_pointer) {
        uint32_t read_count = (data->blocks_in_first_class_pointer - block_idx) < block_count ?
            (data->blocks_in_first_class_pointer - block_idx) : block_count;
        read_first_class_block(data, inode, block_idx, read_count, buffer);
        buffer += read_count * block_size;
        block_count -= read_count;
        total_read_count += read_count;
        block_idx = 0;
    } else {
        block_idx -= data->blocks_in_first_class_pointer;
    }
    if (block_count == 0) return total_read_count;

    if (block_idx < data->blocks_in_second_class_pointer) {
        uint32_t read_count = (data->blocks_in_second_class_pointer - block_idx) < block_count ?
            (data->blocks_in_second_class_pointer - block_idx) : block_count;
        read_second_class_block(data, inode, block_idx, read_count, buffer);
        buffer += read_count * block_size;
        block_count -= read_count;
        total_read_count += read_count;
        block_idx = 0;
    } else {
        block_idx -= data->blocks_in_second_class_pointer;
    }
    if (block_count == 0) return total_read_count;

    if (block_idx < data->blocks_in_third_class_pointer) {
        uint32_t read_count = (data->blocks_in_third_class_pointer - block_idx) < block_count ?
            (data->blocks_in_third_class_pointer - block_idx) : block_count;
        read_third_class_block(data, inode, block_idx, read_count, buffer);
        block_count -= read_count;
        total_read_count += read_count;
        block_idx = 0;
    } else {
        block_idx -= data->blocks_in_third_class_pointer;
    }
    return total_read_count;
}

static int get_inode_by_id(ext2_data* data, uint32_t id, ext2_inode* out_inode) {
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

    data->blocks_in_first_class_pointer = data->dev->block_size / sizeof(uint32_t);
    data->blocks_in_second_class_pointer = data->dev->block_size / sizeof(uint32_t) * data->blocks_in_first_class_pointer;
    data->blocks_in_third_class_pointer = data->dev->block_size / sizeof(uint32_t) * data->blocks_in_second_class_pointer;
    
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