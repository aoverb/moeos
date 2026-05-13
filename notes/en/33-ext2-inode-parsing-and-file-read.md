## Homemade OS (33): Ext2 Filesystem Driver — Inode Parsing, Opening and Reading Files

Let's implement inode parsing and file opening/reading.

### Inode Parsing

We already parsed the inode in the previous chapter. Now we need to implement a function that reads the data blocks corresponding to an inode.

Reading an inode's data content is quite involved — it involves block alignment, offsets, multi-level pointers, cross-level reads... We need to carefully think this through.

#### The Challenge

Reading such a file presents tricky scenarios: the user provides an offset that falls in the middle of a block pointed to by a double indirect pointer, and a length that ends in the middle of a block pointed to by a triple indirect pointer...

In this case, we not only need to locate which specific data blocks correspond to the left and right boundaries and their offsets, but also handle special cases when offsets aren't block-aligned, find the next data block, and be careful not to read beyond the file's length... There are quite a lot of edge conditions to watch out for.

Maybe we should start with a friendlier version — one that assumes **block-aligned reads** as a prerequisite.

With that assumption, we can build the following interface:

```cpp
static size_t read_block_in_inode(ext2_data* data, const ext2_inode* inode, uint32_t block_idx, uint32_t block_count, char* buffer);
```

This interface reads `block_count` consecutive blocks starting at `block_idx` from the inode into the buffer. That doesn't seem so bad now, does it?

Let's use divide-and-conquer (putting the elephant in the fridge) to think about handling block boundary crossings.

Maybe we can split the task into four parts, handled by specialized functions for direct, single, double, and triple indirect pointer block reads. Let's see how to implement that:

```cpp
static size_t read_block_in_inode(ext2_data* data, const ext2_inode* inode,
    uint32_t block_idx, uint32_t block_count, char* buffer) {
    uint32_t total_read_count = 0;
    size_t block_size = data->dev->block_size; 
    if (block_idx < 12) {
        uint32_t read_count = (12 - block_idx) < block_count ? (12 - block_idx) : block_count;
        read_direct_block(data, inode, block_idx, read_count, buffer);
        buffer += read_count * block_size;
        block_count -= read_count;
        total_read_count += read_count;
        block_idx = 0;
    } else {
        block_idx -= 12; // Reset block_idx to make it relative for each pointer level
    }
    if (block_count == 0) return total_read_count; // Early exit for efficiency

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
        buffer += read_count * block_size;
        block_count -= read_count;
        total_read_count += read_count;
        block_idx = 0;
    } else {
        block_idx -= data->blocks_in_third_class_pointer;
    }
    return total_read_count;
}
```

And precompute how many blocks each indirect pointer level can represent at mount time:

```cpp
    data->blocks_in_first_class_pointer  = data->dev->block_size / sizeof(uint32_t);
    data->blocks_in_second_class_pointer = data->dev->block_size / sizeof(uint32_t) * data->blocks_in_first_class_pointer;
    data->blocks_in_third_class_pointer  = data->dev->block_size / sizeof(uint32_t) * data->blocks_in_second_class_pointer;
```

Now let's see how to implement each level.

#### Direct Read

```cpp
void read_direct_block(ext2_data* data, const ext2_inode* inode, uint32_t block_idx,
    uint32_t block_count, char* buffer) {
    size_t block_size = data->dev->block_size;
    for (int i = 0; i < block_count; ++i) {
        uint32_t blk = inode->i_block[block_idx + i];
        if (blk == 0) { // Sparse block, zero it out
            memset(buffer + i * block_size, 0, block_size);
        } else {
            data->dev->read(data->dev, blk, buffer + i * block_size);
        }
    }
    return;
}
```

The only thing to note here is sparse blocks.

#### Single Indirect Pointer

```cpp
void read_first_class_block(ext2_data* data, const ext2_inode* inode, uint32_t block_idx,
    uint32_t block_count, char* buffer) {
    const size_t block_size = data->dev->block_size;
    uint32_t blk = inode->i_block[12]; // Single indirect pointer block
    if (blk == 0) {
        memset(buffer, 0, block_count * block_size);
        return;
    }
    uint32_t* buf = (uint32_t*)kmalloc(block_size);
    if (data->dev->read(data->dev, blk, buf) < 0) {
        // Log error
        kfree(buf);
        return;
    }
    for (int j = 0; j < block_count; ++j) { // Read out direct pointers
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
```

Similar to direct reads — just read the direct pointers from `inode->i_block[12]`.

#### Double Indirect Pointer

Things get complex at the double indirect level!

```cpp
// Too complex... needs to be rewritten with recursion
void read_second_class_block(ext2_data* data, const ext2_inode* inode, uint32_t block_idx,
    uint32_t block_count, char* buffer) {
    const size_t block_size = data->dev->block_size;
    uint32_t blk = inode->i_block[13]; // Double indirect pointer block
    if (blk == 0) {
        memset(buffer, 0, block_count * block_size);
        return;
    }
    uint32_t* buf = (uint32_t*)kmalloc(block_size);
    if (data->dev->read(data->dev, blk, buf) < 0) {
        // Log error
        kfree(buf);
        return;
    }

    uint32_t first_index = (block_idx / data->blocks_in_first_class_pointer);
    uint32_t first_index_offset = block_idx % data->blocks_in_first_class_pointer;
    uint32_t first_left_blk = buf[first_index]; // Leftmost single indirect pointer
    if (first_left_blk != 0) {
        uint32_t* direct_in_left_blk = (uint32_t*)kmalloc(block_size);
        if (data->dev->read(data->dev, first_left_blk, direct_in_left_blk) < 0) {
            // Log error
            kfree(buf);
            kfree(direct_in_left_blk);
            return;
        }

        for (int i = first_index_offset; i < data->blocks_in_first_class_pointer && block_count > 0; ++i) {
            uint32_t drt_buf = direct_in_left_blk[i]; // Direct pointer to data block
            if (drt_buf == 0) {
                memset(buffer, 0, block_size);
            } else if (data->dev->read(data->dev, drt_buf, buffer) < 0) {
                // Log error
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


    // Now handle the middle blocks that can be read fully
    uint32_t* mid_blk_buf = (uint32_t*)kmalloc(block_size);
    uint32_t mid_first_num = block_count / data->blocks_in_first_class_pointer;
    for (int i = 0; i < mid_first_num; ++i) {
        uint32_t mid_blk = buf[first_index + 1 + i];
        if (mid_blk == 0) {
            memset(buffer, 0, data->blocks_in_first_class_pointer * block_size);
            buffer += data->blocks_in_first_class_pointer * block_size;
            continue;
        }
        
        if (data->dev->read(data->dev, mid_blk, mid_blk_buf) < 0) {
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

    // Any remaining blocks on the right side
    if (block_count > 0) {
        uint32_t right_index = first_index + mid_first_num + 1;
        uint32_t right_blk = buf[right_index];
        if (right_blk == 0) {
            memset(buffer, 0, block_count * block_size);
            kfree(buf);
            return;
        }
        uint32_t* right_blk_buf = (uint32_t*)kmalloc(block_size);
        if (data->dev->read(data->dev, right_blk, right_blk_buf) < 0) {
            // Log error
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
```

I'm too exhausted to write the triple indirect pointer version.

#### Recursive Approach

This recursion initially stumped me because I didn't realize that the block granularity I could pass to the lower level was already at the direct pointer block level. I kept trying to figure out how to split child blocks into unequal-left-boundary + middle-big-blocks + unequal-right-boundary (the after-effects of writing the double indirect pointer version...).

Passing `block_idx` relatively to the lower level is done by computing `child_offset` (the child's relative position). Everything about how much to read and how to update values revolves around `child_offset`.

```cpp
// block_idx is the index of leaf blocks (direct pointer blocks) relative to this block,
// and block_count has the same granularity
void read_indirect_block(ext2_data* data, uint32_t cur_block, uint32_t depth, 
    uint32_t block_idx, uint32_t block_count, char* buffer) {
    const size_t block_size = data->dev->block_size;
    if (cur_block == 0) {
        memset(buffer, 0, block_count * block_size);
        return;
    }
    if (depth == 0) { // Direct pointer
        data->dev->read(data->dev, cur_block, buffer);
        return;
    }

    uint32_t* buf = (uint32_t*)kmalloc(block_size);
    if (data->dev->read(data->dev, cur_block, buf) < 0) {
        // Log error
        kfree(buf);
        return;
    }
    uint32_t child_idx = block_idx / data->block_num[depth - 1];
    while (block_count > 0) {
        uint32_t child_blk = buf[child_idx];
        uint32_t child_offset = block_idx % data->block_num[depth - 1];
        uint32_t child_readcount = data->block_num[depth - 1] - child_offset;
        child_readcount = child_readcount < block_count ? child_readcount : block_count;
        read_indirect_block(data, child_blk, depth - 1, child_offset, child_readcount, buffer);
        buffer += child_readcount * block_size;
        block_idx += child_readcount;
        block_count -= child_readcount;
        ++child_idx;
    }
    kfree(buf);
    return;
}
```

This is much more concise. The original `read_block_in_inode` is also updated accordingly:

```cpp
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
        block_idx -= 12;
    }
    if (block_count == 0) return total_read_count;

    if (block_idx < data->block_num[1]) {
        uint32_t read_count = (data->block_num[1] - block_idx) < block_count ?
            (data->block_num[1] - block_idx) : block_count;
        read_indirect_block(data, inode->i_block[12], 1, block_idx, read_count, buffer);
        buffer += read_count * block_size;
        block_count -= read_count;
        total_read_count += read_count;
        block_idx = 0;
    } else {
        block_idx -= data->block_num[1];
    }
    if (block_count == 0) return total_read_count;

    if (block_idx < data->block_num[2]) {
        uint32_t read_count = (data->block_num[2] - block_idx) < block_count ?
            (data->block_num[2] - block_idx) : block_count;
        read_indirect_block(data, inode->i_block[13], 2, block_idx, read_count, buffer);
        buffer += read_count * block_size;
        block_count -= read_count;
        total_read_count += read_count;
        block_idx = 0;
    } else {
        block_idx -= data->block_num[2];
    }
    if (block_count == 0) return total_read_count;

    if (block_idx < data->block_num[3]) {
        uint32_t read_count = (data->block_num[3] - block_idx) < block_count ?
            (data->block_num[3] - block_idx) : block_count;
        read_indirect_block(data, inode->i_block[14], 3, block_idx, read_count, buffer);
        block_count -= read_count;
        total_read_count += read_count;
        block_idx = 0;
    } else {
        block_idx -= data->block_num[3];
    }
    return total_read_count;
}
```

#### From Block Granularity to Byte Granularity

Now let's look at how to read byte-granularity data.

The approach is: read the left unaligned block separately into the buffer → advance the offset → read the full middle blocks → read the right unaligned block into the buffer.

Let's implement a `read` function following this approach:

```cpp
int read(mounting_point* mp, uint32_t inode_id, uint32_t offset, char* buffer, uint32_t size) {
    ext2_data* data = (ext2_data*)mp->data;
    const size_t block_size = data->dev->block_size; 
    ext2_inode* inode = static_cast<ext2_inode*>(kmalloc(sizeof(ext2_inode)));
    if (get_inode_by_id(data, inode_id, inode) < 0) {
        kfree(inode);
        return -1;
    }

    if (offset >= inode->i_size) {
        kfree(inode);
        return 0;
    }

    if (offset + size > inode->i_size) {
        size = inode->i_size - offset;
    }

    uint32_t read_size = size;

    char* tmp_block_buffer = static_cast<char*>(kmalloc(block_size));
    // Read left boundary block
    if (offset % block_size != 0) {
        int left_block = offset / block_size;
        read_block_in_inode(data, inode, left_block, 1, tmp_block_buffer);
        int left_read_size = size < block_size - (offset % block_size) ? size : block_size - (offset % block_size);
        memcpy(buffer, tmp_block_buffer + (offset % block_size), left_read_size);
        offset += left_read_size;
        buffer += left_read_size;
        size -= left_read_size;
    }
    
    // Read middle full blocks
    int mid_block = offset / block_size;
    int mid_block_count = size / block_size;
    read_block_in_inode(data, inode, mid_block, mid_block_count, buffer);
    offset += mid_block_count * block_size;
    buffer += mid_block_count * block_size;
    size -= mid_block_count * block_size;

    // Read right boundary block
    if (size != 0) {
        int right_block = mid_block + mid_block_count;
        read_block_in_inode(data, inode, right_block, 1, tmp_block_buffer);
        memcpy(buffer, tmp_block_buffer, size);
    }
    kfree(tmp_block_buffer);
    kfree(inode);
    return read_size;
}
```

![image-20260316164449567](../assets/自制操作系统（33）：Ext2文件系统驱动——inode解析，打开、读取文件/image-20260316164449567.png)

Time to take it for a spin:

![image-20260316164501583](../assets/自制操作系统（33）：Ext2文件系统驱动——inode解析，打开、读取文件/image-20260316164501583.png)

![image-20260316164522435](../assets/自制操作系统（33）：Ext2文件系统驱动——inode解析，打开、读取文件/image-20260316164522435.png)

...Nothing at all?

![image-20260316165151334](../assets/自制操作系统（33）：Ext2文件系统驱动——inode解析，打开、读取文件/image-20260316165151334.png)

The inode data is incorrect.

![image-20260316165516887](../assets/自制操作系统（33）：Ext2文件系统驱动——inode解析，打开、读取文件/image-20260316165516887.png)

The issue was that the value wasn't being copied. This is the fixed version.

But then I found that the file I specified couldn't be read — inode 77 mapped to 39, inode 75 mapped to 38. This strange behavior was likely due to incorrect inode size:

![image-20260316171719913](../assets/自制操作系统（33）：Ext2文件系统驱动——inode解析，打开、读取文件/image-20260316171719913.png)

![image-20260316171819266](../assets/自制操作系统（33）：Ext2文件系统驱动——inode解析，打开、读取文件/image-20260316171819266.png)

In later versions of ext2, the inode size can be dynamically adjusted... It should be read from the superblock.

After the fix, I still had the issue where reading inode 77 returned 71, and reading inode 79 returned 72...

![image-20260316172446627](../assets/自制操作系统（33）：Ext2文件系统驱动——inode解析，打开、读取文件/image-20260316172446627.png)

Oh — forgot to update this. The offset calculation was still using my struct's size. After fixing it like the example above, everything worked:

![image-20260316172949934](../assets/自制操作系统（33）：Ext2文件系统驱动——inode解析，打开、读取文件/image-20260316172949934.png)

### Block Cache

We can use the existing hash table + doubly linked list to replace I/O reads with cache reads.

Here's an example using the LRU algorithm:

```cpp
constexpr uint32_t CACHE_ENRTY_COUNT = 40;

struct cache_entry {
    int block_no;
    char* data; // Block data
    cache_entry* prev;
    cache_entry* next;
};

struct cache_data {
    cache_entry entry[CACHE_ENRTY_COUNT];
    std::unordered_map<int, cache_entry*> map;
    ext2_data* mp_data;
    cache_entry* head;
    spinlock cacheLock;
};

void detach(cache_entry* item) {
    if (item->prev) item->prev->next = item->next;
    if (item->next) item->next->prev = item->prev;
}

void insert_into_head(cache_data& cache_data, cache_entry* recently_used) {
    if (recently_used == cache_data.head) return;
    detach(recently_used);
    cache_data.head->prev->next = recently_used;
    recently_used->prev = cache_data.head->prev;
    cache_data.head->prev = recently_used;
    recently_used->next = cache_data.head;
    cache_data.head = recently_used;
}

int cache_read(cache_data& cache_data, int block_no, void* block_buffer) {
    SpinlockGuard guard(cache_data.cacheLock);
    auto itr = cache_data.map.find(block_no);
    if (itr != cache_data.map.end()) {
        memcpy(block_buffer, itr->second->data, cache_data.mp_data->dev->block_size);
        insert_into_head(cache_data, itr->second);
        return 0;
    }

    if (cache_data.mp_data->dev->read(cache_data.mp_data->dev, block_no, block_buffer) < 0) {
        return -1;
    }

    // Evict the least recently used entry
    cache_entry* least_rused = cache_data.head->prev;
    detach(least_rused);
    memcpy(least_rused->data, block_buffer, cache_data.mp_data->dev->block_size);
    cache_data.map.erase(least_rused->block_no);
    least_rused->block_no = block_no;
    cache_data.map[block_no] = least_rused;
    insert_into_head(cache_data, least_rused);
    return 0;
}

void init_cache(ext2_data* mp_data) {
    mp_data->cache_data = new (kmalloc(sizeof(cache_data))) cache_data();
    cache_data& cache_data = *(mp_data->cache_data);
    cache_data.mp_data = mp_data;
    cache_data.head = &(cache_data.entry[0]);
    for (int i = 0; i < CACHE_ENRTY_COUNT; ++i) {
        cache_data.entry[i].prev = &(cache_data.entry[(i - 1 + CACHE_ENRTY_COUNT) % CACHE_ENRTY_COUNT]);
        cache_data.entry[i].next = &(cache_data.entry[(i + 1) % CACHE_ENRTY_COUNT]);
        cache_data.entry[i].data = ((char*)kmalloc(mp_data->dev->block_size));
        cache_data.entry[i].block_no = -1;
    }
}
```

---

In the next chapter, we'll parse paths.
