## Homemade OS (34): Ext2 Filesystem Driver — Directory Traversal, Path Component Resolution, Block/Inode Allocator, Cache Flush

Let's first implement `dir_lookup`, a function that finds the inode of a specified filename under a given directory inode.

As mentioned in the previous chapter, directory entries are stored as variable-length structures within the directory file's data blocks. The organizational unit is:

```cpp
struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;   // 1=regular file, 2=directory, 7=symlink ...
    char     name[];      // NOT null-terminated!
};
```

`dir_lookup`:

```cpp
int dir_lookup(ext2_data* data, uint32_t inode_id, const char* name, int insist_type = -1) {
    if (inode_id == 0) return -1;
    const size_t block_size = data->dev->block_size; 
    ext2_inode* inode = static_cast<ext2_inode*>(kmalloc(sizeof(ext2_inode)));
    if (get_inode_by_id(data, inode_id, inode) < 0) {
        kfree(inode);
        return -1;
    }

    uint32_t offset = 0;
    char* tmp_block_buffer = static_cast<char*>(kmalloc(block_size));
    while (offset < inode->i_size) {
        read_block_in_inode(data, inode, offset / block_size, 1, tmp_block_buffer);
        ext2_dir_entry* entry = reinterpret_cast<ext2_dir_entry*>(tmp_block_buffer + offset % block_size);
        if (entry->rec_len == 0) break;
        if (strlen(name) == entry->name_len && strncmp(name, entry->name, strlen(name)) == 0) {
            int inode_num = entry->inode;
            int inode_file_type = entry->file_type;
            kfree(inode);
            kfree(tmp_block_buffer);
            return (insist_type == -1 || insist_type == inode_file_type) ? inode_num : -2;
        }
        offset += entry->rec_len;
    }
    kfree(inode);
    kfree(tmp_block_buffer);
    return 0;
}
```

Note the `insist_type` parameter — it's used to find files of a specific type requested by the caller. This is useful when we implement path resolution later:

```cpp
int relative_lookup(ext2_data* data, uint32_t inode_id, const char* path) {
    if (strlen(path) == 0 || inode_id == 0) return inode_id;
    const size_t block_size = data->dev->block_size; 
    if (path[0] == '/') ++path;
    int par_node = inode_id;
    char name[255];
    int name_len = 0;
    for (int i = 0; i < strlen(path) + 1; ++i) {
        if (path[i] == '/' || path[i] == '\0') {
            if (name_len == 0) break;
            name[name_len++] = '\0';
            par_node = dir_lookup(data, par_node, name, path[i] == '/' ? FILE_TYPE_DIR : -1);
            if (par_node <= 0) break;
            name_len = 0;
        } else {
            name[name_len++] = path[i];
        }
    }
    return par_node;
}
```

You really don't need much more than this. Now we can implement a read-only version of the Ext2 driver!

Let me highlight a few interesting functions.

#### stat

```cpp
int stat(mounting_point* mp, const char* path, file_stat* out) {
    ext2_data* data = (ext2_data*)mp->data;
    if (strcmp(path, "/") == 0) {
        out->group_id = (data->root_inode.i_gid_high << 16) | data->root_inode.i_gid;
        out->owner_id = (data->root_inode.i_uid_high << 16) | data->root_inode.i_uid;
        out->size = data->root_inode.i_fsize;
        out->mode = data->root_inode.i_mode;
        out->last_modified = data->root_inode.i_mtime;
        out->type = 0;
        return 0;
    }
    const size_t block_size = data->dev->block_size; 
    ext2_inode* inode = static_cast<ext2_inode*>(kmalloc(sizeof(ext2_inode)));
    int inode_id = relative_lookup(data, ROOT_INODE_NO, path);
    if (inode_id <= 0) return -1;
    if (get_inode_by_id(data, inode_id, inode) < 0) {
        kfree(inode);
        return -1;
    }
    out->group_id = (inode->i_gid_high << 16) | inode->i_gid;
    out->size = inode->i_size;
    out->mode = inode->i_mode;
    out->last_modified = inode->i_mtime;
    out->name[0] = '\0';
    out->owner_name[0] = '\0';
    out->group_name[0] = '\0';
    out->owner_id = (inode->i_uid_high << 16) | inode->i_uid;
    out->type = (inode->i_mode & 0xF000) >> 12 == MODE_FTYPE_DIR ? 0 : 1;
    kfree(inode);
    return 0;
}
```

In `stat`, since Linux doesn't store as much info as tarfs did, many fields in the output `out` have to be left empty. We could technically convert gid/uid to text and fill them in later.

#### readdir

```cpp
int readdir(mounting_point* mp, uint32_t inode_id, uint32_t offset, dirent* out) {
    if (inode_id == 0) return -1;
    ext2_data* data = (ext2_data*)mp->data;
    const size_t block_size = data->dev->block_size; 
    ext2_inode* inode = static_cast<ext2_inode*>(kmalloc(sizeof(ext2_inode)));
    if (get_inode_by_id(data, inode_id, inode) < 0) {
        kfree(inode);
        return -1;
    }
    
    uint32_t read_offset = 0;
    char* tmp_block_buffer = static_cast<char*>(kmalloc(block_size));
    while (read_offset < inode->i_size) {
        read_block_in_inode(data, inode, read_offset / block_size, 1, tmp_block_buffer);
        ext2_dir_entry* entry = reinterpret_cast<ext2_dir_entry*>(tmp_block_buffer + read_offset % block_size);
        if (entry->rec_len == 0) break;
        if (entry->inode != 0 && offset-- == 0) {
            out->inode = entry->inode;
            strncpy(out->name, entry->name, entry->name_len);
            out->name[entry->name_len] = '\0';
            out->type = entry->file_type;
            kfree(inode);
            kfree(tmp_block_buffer);
            return 1;
        }
        read_offset += entry->rec_len;
    }
    kfree(inode);
    kfree(tmp_block_buffer);
    return 0;
}
```

Still the classic O(n²) traversal — restarting from the beginning each time to reach the current offset. Better than nothing!

#### Result

![image-20260316221037866](../assets/自制操作系统（34）：Ext2文件系统驱动——目录遍历，路径分量解析，块、inode分配器，缓存刷新/image-20260316221037866.png)

The result after adaptation looks pretty good!

At this point, the read-only portion of Ext2 is complete. Next, let's look at how to support writing.

### Block and Inode Allocator

Let's write a block and inode allocator — this is the foundational component for write support.

#### Bitmap

![image-20260315164008469](../assets/自制操作系统（34）：Ext2文件系统驱动——目录遍历，路径分量解析，块、inode分配器，缓存刷新/image-20260315164008469_1.png)

After each block group's descriptor, there's a **data block bitmap** and an **inode bitmap**. The bitmap works like this: if you represent the entire bitmap in binary, then if the n-th bit from the left is 1, the n-th block/inode is occupied; otherwise it's free. Our allocator finds free blocks or inodes in the bitmap, returns the corresponding block number or inode number, and also supports releasing bitmap entries by block/inode number.

Bitmaps are quite efficient. Assuming a block is 1024 bytes, one block bitmap can mark over 7 GB of space and over 8 million inodes — a classic case of spending little to achieve a lot.

#### Finding a Free Block

Finding a free block involves iterating over block groups. First, check the group's descriptor — examine the free block/inode count. If non-zero, search the group's data block bitmap for a free block.

Once found, mark the corresponding bit as 1, update the group descriptor's free block count, and also update the superblock's free block count.

When reading the bitmap, using `cache_read` would `memcpy` the cached data before returning it. It's more efficient to directly return a pointer to the cached data, so we need a function for that:

```cpp
shared_ptr<char> get_cache_ptr(cache_data& cache_data, int block_no) {...}
```

```cpp
// Note: after each single or batch call to alloc/free, call flush_metadata 
// to refresh the block/inode counts in metadata
int block_alloc(ext2_data* data) {
    const size_t block_size = data->dev->block_size; 
    for (int i = 0; i < data->bg_num; ++i) {
        ext2_group_desc& gd = data->gdt[i];
        if (gd.bg_free_blocks_count == 0) continue;
        shared_ptr<char> cache_ptr = get_cache_ptr(*data->cache_data, gd.bg_block_bitmap);
        for (int j = 0; j < block_size; ++j) {
            if (cache_ptr.get()[j] == 0xFF) continue;
            // Found a free block
            // Find the position of the lowest 0 bit
            int pos =  __builtin_ctz(~cache_ptr.get()[j]);
            cache_ptr.get()[j] |= (1 << pos);
            taint(*data->cache_data, gd.bg_block_bitmap);
            --gd.bg_free_blocks_count;
            --data->sb.s_free_blocks_count;
            return (i * data->sb.s_blocks_per_group +
            j * 8 + pos +
            data->sb.s_first_data_block);
        }
    }
    return -1;
}

void block_free(ext2_data* data, int block_no) {
    // Reverse-calculate the group from block_no
    if (block_no == -1) return;
    block_no -= data->sb.s_first_data_block;
    int grp_no = block_no / data->sb.s_blocks_per_group;
    int offset = (block_no % data->sb.s_blocks_per_group) / 8;
    int bit_pos = (block_no % data->sb.s_blocks_per_group) % 8;
    ext2_group_desc& gd = data->gdt[grp_no];
    shared_ptr<char> cache_ptr = get_cache_ptr(*data->cache_data, gd.bg_block_bitmap);
    cache_ptr.get()[offset] &= ~(1 << bit_pos);
    ++gd.bg_free_blocks_count;
    ++data->sb.s_free_blocks_count;
    taint(*data->cache_data, gd.bg_block_bitmap);
}
```

#### Finding a Free Inode

Similar to finding a free block, just remember that the inode number given to users is 1 greater than the inode's index.

#### Metadata Flush

```cpp
void flush_metadata(ext2_data* data) {
    // Write metadata bypassing cache
    // Flush superblock
    void* sb_buffer = kmalloc(data->dev->block_size);
    data->dev->read(data->dev, data->sb_block_num, sb_buffer);
    memcpy((char*)sb_buffer + data->sb_offset, &data->sb, sizeof(ext2_super_block));
    data->dev->write(data->dev, data->sb_block_num, sb_buffer);
    kfree(sb_buffer);
    // Flush group descriptors
    uint32_t bg_block_num = (data->bg_num * sizeof(ext2_group_desc) + data->dev->block_size - 1) / data->dev->block_size;

    // One block can hold data->dev->block_size / sizeof(ext2_group_desc) descriptors
    uint32_t gd_per_block = (data->dev->block_size / sizeof(ext2_group_desc));
    
    for (int blk_idx = 0; blk_idx < bg_block_num; ++blk_idx) {
        data->dev->write(data->dev, data->sb_block_num + 1 + blk_idx, 
            (void*)&data->gdt[blk_idx * gd_per_block]); // GDT block is right after the superblock block
    }
}
```

Metadata flush bypasses the cache — this is more efficient since these infrequently modified metadata blocks don't need to occupy cache slots.

#### Dirty Bits and Cache Flush

Now that we're using block caching and starting to modify data, we can add a **dirty bit** to cached blocks to mark whether the block's data needs to be written back to disk.

```cpp
struct cache_entry {
    int block_no;
    char* data; // Block data
    cache_entry* prev;
    cache_entry* next;
    bool dirty; // Whether it needs to be written back
};
```

Then we can build a `flush` function that writes dirty blocks from the cache back to disk. This function can be called after each data write, or when a cache block is about to be evicted — very useful.

```cpp
void flush_block(cache_data& cache_data, cache_entry* cache) {
    if (cache->block_no == -1 || !cache->dirty) return;
    cache_data.mp_data->dev->write(cache_data.mp_data->dev, cache->block_no, cache->data);
}

...

// Evict the least recently used entry
cache_entry* least_rused = cache_data.head->prev;
detach(least_rused);
// Write back to disk
flush_block(cache_data, least_rused);
memcpy(least_rused->data, block_buffer, cache_data.mp_data->dev->block_size);
cache_data.map.erase(least_rused->block_no);
```

And a function for marking dirty bits:

```cpp
void taint(cache_data& cache_data, int block_no) {
    SpinlockGuard guard(cache_data.cacheLock);
    const auto& itr = cache_data.map.find(block_no);
    if (itr == cache_data.map.end()) {
        return;
    }
    itr->second->dirty = true;
}
```

#### Cache Write-Back Strategy

When to write the cache back to disk is also worth discussing. Currently, we only write back when a cache block is evicted. As for whether to immediately flush block data and metadata after calling `alloc`/`free` functions, we'll discuss that in the next chapter alongside the actual implementation.

---

In the next chapter, we'll implement some infrastructure functions for file modification and the write functions that use them.
