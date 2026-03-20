#include <driver/vfs.hpp>
#include <driver/ext2.hpp>
#include <kernel/mm.hpp>
#include <unordered_map>
#include <kernel/spinlock.hpp>
#include <shared_ptr>

fs_operation ext2_fs_operation;

constexpr uint32_t CACHE_ENRTY_COUNT = 40;

struct cache_entry {
    int block_no;
    shared_ptr<char> data; // 块数据
    cache_entry* prev;
    cache_entry* next;
    bool dirty;
};

struct cache_data {
    cache_entry entry[CACHE_ENRTY_COUNT];
    std::unordered_map<int, cache_entry*> map;
    ext2_data* mp_data;
    cache_entry* head;
    spinlock cacheLock;
};

static void flush_block(cache_data& cache_data, cache_entry* cache) {
    if (cache->block_no == -1 || !cache->dirty) return;
    cache_data.mp_data->dev->write(cache_data.mp_data->dev, cache->block_no, cache->data.get());
    cache->dirty = false;
}

static void flush_all_block(cache_data& cache_data) {
    SpinlockGuard guard(cache_data.cacheLock);
    for (int i = 0; i < CACHE_ENRTY_COUNT; ++i) {
        flush_block(cache_data, &cache_data.entry[i]);
        cache_data.entry[i].dirty = false;
    }
}

static void taint(cache_data& cache_data, int block_no) {
    SpinlockGuard guard(cache_data.cacheLock);
    const auto& itr = cache_data.map.find(block_no);
    if (itr == cache_data.map.end()) {
        return;
    }
    itr->second->dirty = true;
}

static void detach(cache_entry* item) {
    if (item->prev) item->prev->next = item->next;
    if (item->next) item->next->prev = item->prev;
    item->prev = item->next = nullptr;
}

static void insert_into_head(cache_data& cache_data, cache_entry* recently_used) {
    if (recently_used == cache_data.head) return;
    detach(recently_used);
    cache_data.head->prev->next = recently_used;
    recently_used->prev = cache_data.head->prev;
    cache_data.head->prev = recently_used;
    recently_used->next = cache_data.head;
    cache_data.head = recently_used;
}

static shared_ptr<char> get_cache_ptr(cache_data& cache_data, int block_no) {
    SpinlockGuard guard(cache_data.cacheLock);
    auto itr = cache_data.map.find(block_no);
    if (itr != cache_data.map.end()) {
        insert_into_head(cache_data, itr->second);
        return itr->second->data;
    }

    // 把队列里面最近没使用的记录拿出来换成自己的
    cache_entry* least_rused = cache_data.head->prev;
    // 写回到磁盘
    flush_block(cache_data, least_rused);
    if (cache_data.mp_data->dev->read(cache_data.mp_data->dev, block_no, least_rused->data.get()) < 0) {
        return nullptr;
    }
    detach(least_rused);
    cache_data.map.erase(least_rused->block_no);
    least_rused->block_no = block_no;
    cache_data.map[block_no] = least_rused;
    insert_into_head(cache_data, least_rused);
    return least_rused->data;
}

static int cache_read(cache_data& cache_data, int block_no, void* block_buffer) {
    shared_ptr<char> ptr = get_cache_ptr(cache_data, block_no);
    if (!ptr) return -1;
    memcpy(block_buffer, ptr.get(), cache_data.mp_data->dev->block_size);
    return 0;
}

static void init_cache(ext2_data* mp_data) {
    mp_data->cache_data = new (kmalloc(sizeof(cache_data))) cache_data();
    cache_data& cache_data = *(mp_data->cache_data);
    cache_data.mp_data = mp_data;
    cache_data.head = &(cache_data.entry[0]);
    for (int i = 0; i < CACHE_ENRTY_COUNT; ++i) {
        cache_data.entry[i].prev = &(cache_data.entry[(i - 1 + CACHE_ENRTY_COUNT) % CACHE_ENRTY_COUNT]);
        cache_data.entry[i].next = &(cache_data.entry[(i + 1) % CACHE_ENRTY_COUNT]);
        cache_data.entry[i].data = ((char*)kmalloc(mp_data->dev->block_size));
        cache_data.entry[i].block_no = -1;
        cache_data.entry[i].dirty = false;
    }
}

// 注意：每次单次、批量调用alloc/free之后，需要调用flush metadata刷新元数据里面的块计数/inode计数
static int block_alloc(ext2_data* data) {
    const size_t block_size = data->dev->block_size; 
    for (int i = 0; i < data->bg_num; ++i) {
        ext2_group_desc& gd = data->gdt[i];
        if (gd.bg_free_blocks_count == 0) continue;
        shared_ptr<char> cache_ptr = get_cache_ptr(*data->cache_data, gd.bg_block_bitmap);
        for (int j = 0; j < block_size; ++j) {
            if ((uint8_t)cache_ptr.get()[j] == 0xFF) continue;
            // 找到了空闲的块
            // 下面找到最低的0在哪个位置
            int pos = __builtin_ctz(~(uint8_t)cache_ptr.get()[j]);
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

static void block_free(ext2_data* data, int block_no) {
    // block_no反推所在组
    if (block_no <= 0) return;
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

static int inode_alloc(ext2_data* data) {
    for (int i = 0; i < data->bg_num; ++i) {
        ext2_group_desc& gd = data->gdt[i];
        if (gd.bg_free_inodes_count == 0) continue;
        shared_ptr<char> cache_ptr = get_cache_ptr(*data->cache_data, gd.bg_inode_bitmap);
        for (int j = 0; j < data->sb.s_inodes_per_group / 8; ++j) {
            if ((uint8_t)cache_ptr.get()[j] == 0xFF) continue;
            // 找到了空闲的inode
            // 下面找到最低的0在哪个位置
            int pos = __builtin_ctz(~(uint8_t)cache_ptr.get()[j]);
            cache_ptr.get()[j] |= (1 << pos);
            taint(*data->cache_data, gd.bg_inode_bitmap);
            --gd.bg_free_inodes_count;
            --data->sb.s_free_inodes_count;
            return (i * data->sb.s_inodes_per_group +
            j * 8 + pos) + 1;
        }
    }
    return -1;
}

static void inode_free(ext2_data* data, int inode_no) {
    if (inode_no <= 0) return;
    inode_no -= 1;
    int grp_no = inode_no / data->sb.s_inodes_per_group;
    int offset = (inode_no % data->sb.s_inodes_per_group) / 8;
    int bit_pos = (inode_no % data->sb.s_inodes_per_group) % 8;
    ext2_group_desc& gd = data->gdt[grp_no];
    shared_ptr<char> cache_ptr = get_cache_ptr(*data->cache_data, gd.bg_inode_bitmap);
    cache_ptr.get()[offset] &= ~(1 << bit_pos);
    ++gd.bg_free_inodes_count;
    ++data->sb.s_free_inodes_count;
    taint(*data->cache_data, gd.bg_inode_bitmap);
}

static void read_direct_block(ext2_data* data, const ext2_inode* inode, uint32_t block_idx,
    uint32_t block_count, char* buffer) {
    const size_t block_size = data->dev->block_size;
    for (int i = 0; i < block_count; ++i) {
        uint32_t blk = inode->i_block[block_idx + i];
        if (blk == 0) { // 空洞块，清零即可
            memset(buffer + i * block_size, 0, block_size);
        } else {
            cache_read(*data->cache_data, blk, buffer + i * block_size);
        }
    }
    return;
}

static void flush_metadata(ext2_data* data) {
    // 写元数据不走缓存
    // 刷新超级块
    void* sb_buffer = kmalloc(data->dev->block_size);
    data->dev->read(data->dev, data->sb_block_num, sb_buffer);
    memcpy((char*)sb_buffer + data->sb_offset, &data->sb, sizeof(ext2_super_block));
    data->dev->write(data->dev, data->sb_block_num, sb_buffer);
    kfree(sb_buffer);
    // 刷新组描述符
    uint32_t bg_block_num = (data->bg_num * sizeof(ext2_group_desc) + data->dev->block_size - 1) / data->dev->block_size;

    // 一个块最多可以读出 data->dev->block_size / sizeof(ext2_group_desc) 个组描述符
    uint32_t gd_per_block = (data->dev->block_size / sizeof(ext2_group_desc));
    
    for (int blk_idx = 0; blk_idx < bg_block_num; ++blk_idx) {
        data->dev->write(data->dev, data->sb_block_num + 1 + blk_idx, (void*)&data->gdt[blk_idx * gd_per_block]); // 组描述符表所在的块紧跟超级块所在的块
    }
}

// block_idx是相对于这个块来说的其叶子块（即直接指针块）的索引，block_count粒度也是如此
static void read_indirect_block(ext2_data* data, uint32_t cur_block, uint32_t depth, 
    uint32_t block_idx, uint32_t block_count, char* buffer) {
    const size_t block_size = data->dev->block_size;
    if (cur_block == 0) {
        memset(buffer, 0, block_count * block_size);
        return;
    }
    if (depth == 0) { // 直接指针
        cache_read(*data->cache_data, cur_block, buffer);
        return;
    }

    uint32_t* buf = (uint32_t*)kmalloc(block_size);
    if (cache_read(*data->cache_data, cur_block, buf) < 0) {
        // 记录错误
        kfree(buf);
        return;
    }

    // 这里的next_level_child_idx是本块的下一级子块的索引，其粒度易与本函数入参的块粒度混淆，要注意
    // 事实上只有next_level_child_idx是本块的下一级子块粒度
    uint32_t next_level_child_idx = block_idx / data->block_num[depth - 1];
    while (block_count > 0) { // 读出来的是下一级的指针
        uint32_t next_level_child_blk = buf[next_level_child_idx];
        uint32_t child_offset = block_idx % data->block_num[depth - 1];
        uint32_t child_readcount = data->block_num[depth - 1] - child_offset;
        child_readcount = child_readcount < block_count ? child_readcount : block_count;
        read_indirect_block(data, next_level_child_blk, depth - 1, child_offset, child_readcount, buffer);
        buffer += child_readcount * block_size;
        block_idx += child_readcount;
        block_count -= child_readcount;
        ++next_level_child_idx;
    }
    kfree(buf);
    return;
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

constexpr uint32_t SECTOR_SIZE = 512;

static int get_phys_block_no_in_inode(ext2_data* data, ext2_inode* inode, uint32_t idx) {
    const size_t block_size = data->dev->block_size; 
    const size_t blkcnt_per_block = block_size / 4;
    if (idx < 12) { // 可以通过加直接指针解决
        return inode->i_block[idx];
    }

    idx -= 12;

    if (idx < data->block_num[1]) {
        if (inode->i_block[12] == 0) return 0;
        auto direct_ptr = get_cache_ptr(*data->cache_data, inode->i_block[12]);
        return *((uint32_t*)direct_ptr.get() + idx);
    }

    idx -= data->block_num[1];
    if (idx < data->block_num[2]) {
        if (inode->i_block[13] == 0) return 0;
        auto first_class_ptr = get_cache_ptr(*data->cache_data, inode->i_block[13]);
        uint32_t* first_class = (uint32_t*)first_class_ptr.get() +
            idx / blkcnt_per_block;
        if (*first_class == 0) return 0;
        auto direct_ptr = get_cache_ptr(*data->cache_data, *first_class);
        return *((uint32_t*)direct_ptr.get() + idx % blkcnt_per_block);
    }

    idx -= data->block_num[2];
    if (idx < data->block_num[3]) {
        if (inode->i_block[14] == 0) return 0;
        auto second_class_ptr = get_cache_ptr(*data->cache_data, inode->i_block[14]);
        uint32_t* second_class = (uint32_t*)second_class_ptr.get() +
                                  idx / blkcnt_per_block / blkcnt_per_block;
        if (*second_class == 0) return 0;
        auto first_class_ptr = get_cache_ptr(*data->cache_data, *second_class);
        uint32_t* first_class = (uint32_t*)first_class_ptr.get() +
            (idx / blkcnt_per_block) % blkcnt_per_block;
        if (*first_class == 0) return 0;
        auto direct_ptr = get_cache_ptr(*data->cache_data, *first_class);
        return *((uint32_t*)direct_ptr.get() + idx % blkcnt_per_block);

    }

    return 0;
}

// 在insert_idx插入一个指定的物理块，如果这个地方已经有一个物理块，函数返回-1
// 如果replace为true，强制替换，注意，替换不会自动释放原有的物理块！
static size_t insert_block_in_inode(ext2_data* data, ext2_inode* inode,
    uint32_t block_no, uint32_t insert_idx, bool replace = false) {
    const size_t block_size = data->dev->block_size; 
    const size_t blkcnt_per_block = block_size / 4;
    const ext2_super_block& sb = data->sb;
    if (insert_idx < 12) { // 可以通过加直接指针解决
        if (inode->i_block[insert_idx] != 0 && !replace) return -1;
        inode->i_block[insert_idx] = block_no;
        inode->i_blocks += block_size / SECTOR_SIZE;
        return 0;
    }

    insert_idx -= 12;

    if (insert_idx < data->block_num[1]) {
        if (inode->i_block[12] == 0) {
            if (sb.s_free_blocks_count == 0) return -1;
            inode->i_block[12] = block_alloc(data);
            inode->i_blocks += block_size / SECTOR_SIZE;
            memset(get_cache_ptr(*data->cache_data, inode->i_block[12]).get(), 0, block_size);
        }
        auto direct_ptr = get_cache_ptr(*data->cache_data, inode->i_block[12]);
        if (*((uint32_t*)direct_ptr.get() + insert_idx) != 0 && !replace) return -1;
        *((uint32_t*)direct_ptr.get() + insert_idx) = block_no;
        taint(*data->cache_data, inode->i_block[12]);
        inode->i_blocks += block_size / SECTOR_SIZE;
        return 0;
    }

    insert_idx -= data->block_num[1];
    if (insert_idx < data->block_num[2]) {
        if (inode->i_block[13] == 0) {
            if (sb.s_free_blocks_count < 2) return -1;
            inode->i_block[13] = block_alloc(data); // 为二级指针分配空间
            inode->i_blocks += block_size / SECTOR_SIZE;
            memset(get_cache_ptr(*data->cache_data, inode->i_block[13]).get(), 0, block_size);
            taint(*data->cache_data, inode->i_block[13]);
        }
        // 这里必须拷贝一份指针来确保我们的缓存块和对应的指针不会被释放掉
        // 只要不是生命周期原地结束的裸指针，都得这么做
        auto first_class_ptr = get_cache_ptr(*data->cache_data, inode->i_block[13]);
        uint32_t* first_class = (uint32_t*)first_class_ptr.get() +
            insert_idx / blkcnt_per_block;
        if (*first_class == 0) {
            if (sb.s_free_blocks_count == 0) return -1;
            *first_class = block_alloc(data);
            inode->i_blocks += block_size / SECTOR_SIZE;
            memset(get_cache_ptr(*data->cache_data, *first_class).get(), 0, block_size);
            taint(*data->cache_data, inode->i_block[13]);
            taint(*data->cache_data, *first_class);
        }
        auto direct_ptr = get_cache_ptr(*data->cache_data, *first_class);
        if (*((uint32_t*)direct_ptr.get() +
            insert_idx % blkcnt_per_block) != 0 && !replace) return -1;
        *((uint32_t*)direct_ptr.get() + insert_idx % blkcnt_per_block) = block_no;
        taint(*data->cache_data, *first_class);
        inode->i_blocks += block_size / SECTOR_SIZE;
        return 0;
    }

    insert_idx -= data->block_num[2];
    if (insert_idx < data->block_num[3]) {
        if (inode->i_block[14] == 0) {
            if (sb.s_free_blocks_count < 3) return -1;
            inode->i_block[14] = block_alloc(data); // 为三级指针分配空间
            inode->i_blocks += block_size / SECTOR_SIZE;
            memset(get_cache_ptr(*data->cache_data, inode->i_block[14]).get(), 0, block_size);
            taint(*data->cache_data, inode->i_block[14]);
        }
        auto second_class_ptr = get_cache_ptr(*data->cache_data, inode->i_block[14]);
        uint32_t* second_class = (uint32_t*)second_class_ptr.get() +
                                  insert_idx / blkcnt_per_block / blkcnt_per_block;
        if (*second_class == 0) {
            if (sb.s_free_blocks_count < 2) return -1;
            *second_class = block_alloc(data);
            inode->i_blocks += block_size / SECTOR_SIZE;
            taint(*data->cache_data, inode->i_block[14]);
            memset(get_cache_ptr(*data->cache_data, *second_class).get(), 0, block_size);
            taint(*data->cache_data, *second_class);
        }
        auto first_class_ptr = get_cache_ptr(*data->cache_data, *second_class);
        uint32_t* first_class = (uint32_t*)first_class_ptr.get() +
            (insert_idx / blkcnt_per_block) % blkcnt_per_block;
        if (*first_class == 0) {
            if (sb.s_free_blocks_count == 0) return -1;
            *first_class = block_alloc(data);
            inode->i_blocks += block_size / SECTOR_SIZE;
            taint(*data->cache_data, *second_class);
            memset(get_cache_ptr(*data->cache_data, *first_class).get(), 0, block_size);
            taint(*data->cache_data, *first_class);
        }
        auto direct_ptr = get_cache_ptr(*data->cache_data, *first_class);
        if (*((uint32_t*)direct_ptr.get() + insert_idx % blkcnt_per_block) != 0 && !replace) {
            return -1;
        }
        *((uint32_t*)direct_ptr.get() + insert_idx % blkcnt_per_block) = block_no;
        taint(*data->cache_data, *first_class);
        inode->i_blocks += block_size / SECTOR_SIZE;
        return 0;
    }

    return -1;
}

static int set_inode_by_id(ext2_data* data, uint32_t id, const ext2_inode* in_inode) {
    if (id == 0) return -1;
    ext2_super_block& sb = data->sb;
    uint32_t inodes_per_group = sb.s_inodes_per_group;
    // 先算算我在哪个组
    uint32_t idx = id - 1;
    
    uint32_t group_idx = idx / inodes_per_group;
    
    ext2_group_desc& gd = data->gdt[group_idx];
    
    uint32_t inode_table_block_id = gd.bg_inode_table; // 这里拿到的是bg_inode_table开始的块号

    size_t inode_size = data->inode_size;
    size_t block_size = data->dev->block_size;
    uint32_t inodes_count_in_each_block = block_size / inode_size;

    // 我们还得算一个相对的id，也就是在这个块组里面的id
    uint32_t idx_in_group = idx % inodes_per_group;

    uint32_t block_idx = idx_in_group / inodes_count_in_each_block;
    uint32_t offset = idx_in_group % inodes_count_in_each_block;

    *reinterpret_cast<ext2_inode*>((get_cache_ptr(*data->cache_data, inode_table_block_id + block_idx).get() +
        offset * inode_size)) = *in_inode;
    taint(*data->cache_data, inode_table_block_id + block_idx);
    return 0;
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

    size_t inode_size = data->inode_size;
    size_t block_size = data->dev->block_size;
    uint32_t inodes_count_in_each_block = block_size / inode_size;

    // 我们还得算一个相对的id，也就是在这个块组里面的id
    uint32_t idx_in_group = idx % inodes_per_group;

    uint32_t block_idx = idx_in_group / inodes_count_in_each_block;
    uint32_t offset = idx_in_group % inodes_count_in_each_block;

    void* buffer = kmalloc(block_size);
    cache_read(*data->cache_data, inode_table_block_id + block_idx,  buffer);
    *out_inode = *reinterpret_cast<ext2_inode*>((static_cast<char*>(buffer) + offset * inode_size));
    kfree(buffer);
    return 0;
}

static int mount(mounting_point* mp) {
    // 千里之行，始于足下...
    ext2_data* data = (ext2_data*)mp->data;

    // 读取超级块，超级块是在0号块之后偏移1024字节处
    data->sb_block_num = 1024 / data->dev->block_size;
    data->sb_offset = 1024 % data->dev->block_size;
    void* buffer = kmalloc(data->dev->block_size);
    int ret = data->dev->read(data->dev, data->sb_block_num, buffer);
    if (ret < 0) {
        kfree(buffer);
        return -1; // 读取超级块失败
    }
    ext2_super_block* ext2_sb = reinterpret_cast<ext2_super_block*>((char*)buffer + data->sb_offset);
    data->sb = *ext2_sb;
    kfree(buffer);

    ext2_super_block& sb = data->sb;

    if (data->sb.s_rev_level >= 1) {
        data->inode_size = data->sb.s_inode_size;
    } else {
        data->inode_size = 128;
    }
    // 检查魔数...
    if (sb.s_magic != 0xEF53) {
        return -1;
    }
    // 检查磁盘状态
    // 我们可以不做...
    // sb.s_state...

    // 初始化块缓存
    init_cache(data);
    // 缓存组描述符表
    data->bg_num = (sb.s_blocks_count + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;
    uint32_t gdt_alloc = ((sizeof(ext2_group_desc) * data->bg_num + data->dev->block_size - 1) /
                         data->dev->block_size) * data->dev->block_size;
    data->gdt = (ext2_group_desc*)kmalloc(gdt_alloc);
    memset(data->gdt, 0, gdt_alloc);

    // 读多少个块才能把所有的块组描述符读出来？
    uint32_t bg_block_num = (data->bg_num * sizeof(ext2_group_desc) + data->dev->block_size - 1) / data->dev->block_size;

    uint32_t gd_idx = 0;

    // 一个块最多可以读出 data->dev->block_size / sizeof(ext2_group_desc) 个组描述符
    uint32_t gd_per_block = (data->dev->block_size / sizeof(ext2_group_desc));
    for (int blk_idx = 0; blk_idx < bg_block_num; ++blk_idx) {
        void* gd_buffer = kmalloc(data->dev->block_size);
        cache_read(*data->cache_data, data->sb_block_num + 1 + blk_idx, gd_buffer); // 组描述符表所在的块紧跟超级块所在的块
        
        for (int i = 0; i < gd_per_block; ++i) {
            data->gdt[gd_idx++] = ((ext2_group_desc*)gd_buffer)[i];
            if (gd_idx == data->bg_num) {
                break;
            }
        }

        kfree(gd_buffer);
    }

    data->block_num[0] = 1;
    data->block_num[1] = data->block_num[0] * data->dev->block_size / sizeof(uint32_t);
    data->block_num[2] = data->block_num[1] * data->dev->block_size / sizeof(uint32_t);
    data->block_num[3] = data->block_num[2] * data->dev->block_size / sizeof(uint32_t);
    
    get_inode_by_id(data, 2, &data->root_inode);
    return 0;
}

constexpr uint8_t FILE_TYPE_FILE = 1;
constexpr uint8_t FILE_TYPE_DIR = 2;

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

int add_entry_to_dir(ext2_data* data, uint32_t dir_inode_no, uint32_t entry_inode_no, uint8_t file_type,
    const char* name) {
    ext2_inode dir_inode;
    if (get_inode_by_id(data, dir_inode_no, &dir_inode) < 0) {
        return -1;
    }
    uint16_t rec_len;
    rec_len = sizeof(entry_inode_no) + sizeof(rec_len) + sizeof(file_type) + sizeof(uint8_t) + // 最后一个是name_len
              + strlen(name);
    rec_len = (rec_len + 3) / 4 * 4; // 四字节对齐

    const size_t block_size = data->dev->block_size; 

    uint32_t block_num = (dir_inode.i_size + block_size - 1) / block_size;
    for (int i = 0; i < block_num; ++i) {
        uint32_t read_offset = 0;
        int phys_block_no = get_phys_block_no_in_inode(data, &dir_inode, i);
        auto cache_ptr = get_cache_ptr(*data->cache_data, phys_block_no);
        while (read_offset < block_size) {
            ext2_dir_entry* entry = reinterpret_cast<ext2_dir_entry*>(cache_ptr.get() + read_offset);
            size_t old_entry_actual_len = (8 + entry->name_len + 3) / 4 * 4;
            if (entry->rec_len == 0) break;
            if (entry->rec_len > old_entry_actual_len && entry->rec_len - old_entry_actual_len >= rec_len) {
                ext2_dir_entry* new_entry = (ext2_dir_entry*)(cache_ptr.get() + read_offset + old_entry_actual_len);
                new_entry->file_type = file_type;
                new_entry->inode = entry_inode_no;
                memcpy(new_entry->name, name, strlen(name));
                new_entry->name_len = strlen(name);
                // 记得更新旧条目的rec_len
                
                size_t remain_len = entry->rec_len - old_entry_actual_len;
                entry->rec_len = old_entry_actual_len;
                new_entry->rec_len = remain_len;
                taint(*data->cache_data, phys_block_no);
                return 0;
            }
            read_offset += entry->rec_len;
        }
    }

    // 找不到可写的地方，插入新块写入
    int new_blk = block_alloc(data);
    if (new_blk < 0) return -1;
    insert_block_in_inode(data, &dir_inode, new_blk, block_num);
    auto cache_ptr = get_cache_ptr(*data->cache_data, new_blk);
    memset(cache_ptr.get(), 0, block_size);
    ext2_dir_entry* new_entry = (ext2_dir_entry*)cache_ptr.get();
    new_entry->file_type = file_type;
    new_entry->inode = entry_inode_no;
    memcpy(new_entry->name, name, strlen(name));
    new_entry->name_len = strlen(name);
    new_entry->rec_len = block_size;
    taint(*data->cache_data, new_blk);

    dir_inode.i_size += block_size;
    set_inode_by_id(data, dir_inode_no, &dir_inode);
    return 0;
}

int unmount(mounting_point* mp) {
    return -1;
}

void init_inode(ext2_inode& inode, uint8_t type) {
    memset(&inode, 0, sizeof(inode));
    // inode.i_atime = inode.i_ctime = inode.i_dtime = inode.i_mtime = 0; // todo: 我现在还不支持获取时间...
    inode.i_mode = (type == FILE_TYPE_DIR) ? 0x4000 | 0755 : 0x8000 | 0644;
    inode.i_links_count = (type == FILE_TYPE_DIR) ? 2 : 1;
}

void split_path(const char* full_path, char* path, char* name) {
    int last_slash = 0;
    for (int i = strlen(full_path); i >= 0; --i) {
        if (full_path[i] == '/') {
            last_slash = i;
            break;
        }
    }
    if (last_slash == 0) { // 根目录
        strcpy(path, "/");
    } else {
        strncpy(path, full_path, last_slash);
        path[last_slash] = '\0';
    }
    strcpy(name, full_path + last_slash + 1);
}

static int truncate(mounting_point* mp, uint32_t inode_id, uint32_t new_size) {
    ext2_inode inode;
    ext2_data* data = (ext2_data*)mp->data;
    if (get_inode_by_id(data, inode_id, &inode) < 0) return -1;

    uint32_t block_size = data->dev->block_size;
    uint32_t old_blocks = (inode.i_size + block_size - 1) / block_size;
    uint32_t new_blocks = (new_size + block_size - 1) / block_size;

    // 只处理缩小，扩大的情况 write 时自动分配
    if (new_size >= inode.i_size) {
        inode.i_size = new_size;
        set_inode_by_id(data, inode_id, &inode);
        return 0;
    }

    // 释放多余的数据块
    for (uint32_t i = new_blocks; i < old_blocks; i++) {
        int phys = get_phys_block_no_in_inode(data, &inode, i);
        if (phys > 0) {
            block_free(data, phys);
            insert_block_in_inode(data, &inode, 0, i, true);  // 清零指针
            inode.i_blocks -= block_size / SECTOR_SIZE;
        }
    }

    // 释放不再需要的间接块
    uint32_t ptrs_per_block = block_size / 4;

    // 一级间接：如果所有间接引用的块都被释放了
    if (new_blocks <= 12 && inode.i_block[12] != 0) {
        block_free(data, inode.i_block[12]);
        inode.i_blocks -= block_size / SECTOR_SIZE;
        inode.i_block[12] = 0;
    }

    // 二级间接
    if (new_blocks <= 12 + ptrs_per_block && inode.i_block[13] != 0) {
        // 释放所有一级间接子块
        auto dind = get_cache_ptr(*data->cache_data, inode.i_block[13]);
        for (uint32_t i = 0; i < ptrs_per_block; i++) {
            uint32_t ind_block = ((uint32_t*)dind.get())[i];
            if (ind_block != 0) {
                block_free(data, ind_block);
                inode.i_blocks -= block_size / SECTOR_SIZE;
            }
        }
        block_free(data, inode.i_block[13]);
        inode.i_blocks -= block_size / SECTOR_SIZE;
        inode.i_block[13] = 0;
    }

    // 三级间接
    if (new_blocks <= 12 + ptrs_per_block + ptrs_per_block * ptrs_per_block 
        && inode.i_block[14] != 0) {
        auto tind = get_cache_ptr(*data->cache_data, inode.i_block[14]);
        for (uint32_t i = 0; i < ptrs_per_block; i++) {
            uint32_t dind_block = ((uint32_t*)tind.get())[i];
            if (dind_block == 0) continue;
            auto dind = get_cache_ptr(*data->cache_data, dind_block);
            for (uint32_t j = 0; j < ptrs_per_block; j++) {
                uint32_t ind_block = ((uint32_t*)dind.get())[j];
                if (ind_block != 0) {
                    block_free(data, ind_block);
                    inode.i_blocks -= block_size / SECTOR_SIZE;
                }
            }
            block_free(data, dind_block);
            inode.i_blocks -= block_size / SECTOR_SIZE;
        }
        block_free(data, inode.i_block[14]);
        inode.i_blocks -= block_size / SECTOR_SIZE;
        inode.i_block[14] = 0;
    }

    // 如果缩小到非块边界，清零最后一个块的尾部
    // 防止读到旧数据
    if (new_size > 0 && new_size % block_size != 0) {
        uint32_t last_block = new_blocks - 1;
        int phys = get_phys_block_no_in_inode(data, &inode, last_block);
        if (phys > 0) {
            auto ptr = get_cache_ptr(*data->cache_data, phys);
            uint32_t tail_offset = new_size % block_size;
            memset(ptr.get() + tail_offset, 0, block_size - tail_offset);
            taint(*data->cache_data, phys);
        }
    }

    inode.i_size = new_size;
    set_inode_by_id(data, inode_id, &inode);
    return 0;
}

constexpr uint32_t ROOT_INODE_NO = 2;

static int create(ext2_data* data, const char* path, const char* name, int type) {
    int path_inode_no = relative_lookup(data, ROOT_INODE_NO, path);
    if (path_inode_no <= 0) {
        return -1; // 路径都不存在，没救了
    }
    int new_inode_no = inode_alloc(data);
    if (new_inode_no <= 0) return -1;
    ext2_inode new_inode;
    init_inode(new_inode, type);
    set_inode_by_id(data, new_inode_no, &new_inode);
    add_entry_to_dir(data, path_inode_no, new_inode_no, type, name);
    flush_all_block(*data->cache_data);
    flush_metadata(data);
    return new_inode_no;
}

static int open(mounting_point* mp, const char* path, uint8_t mode) {
    if (strlen(path) >= 256) return -1; // 不支持这么长的路径
    ext2_data* data = (ext2_data*)mp->data;
    if (path[0] != '/') return -1;
    char par_path[256];
    char name[256];
    split_path(path, par_path, name);
    if (name[0] == '\0') return -1;
    int inode_no = relative_lookup(data, ROOT_INODE_NO, path);
    if (inode_no > 0) {
        if (mode & O_TRUNC) {
            truncate(mp, inode_no, 0);
            flush_all_block(*data->cache_data);
            flush_metadata(data);
        }
        return inode_no;
    }
    if (inode_no <= 0 && (mode & O_CREATE) == 0) return -1;
    return create(data, par_path, name, FILE_TYPE_FILE);
}

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
    // 读左端点块
    if (offset % block_size != 0) {
        int left_block = offset / block_size;
        read_block_in_inode(data, inode, left_block, 1, tmp_block_buffer);
        int left_read_size = size < block_size - (offset % block_size) ? size : block_size - (offset % block_size);
        memcpy(buffer, tmp_block_buffer + (offset % block_size), left_read_size);
        offset += left_read_size;
        buffer += left_read_size;
        size -= left_read_size;
    }
    
    // 读中间的完整块
    int mid_block = offset / block_size;
    int mid_block_count = size / block_size;
    read_block_in_inode(data, inode, mid_block, mid_block_count, buffer);
    offset += mid_block_count * block_size;
    buffer += mid_block_count * block_size;
    size -= mid_block_count * block_size;

    // 读右端点块
    if (size != 0) {
        int right_block = mid_block + mid_block_count;
        read_block_in_inode(data, inode, right_block, 1, tmp_block_buffer);
        memcpy(buffer, tmp_block_buffer, size);
    }
    kfree(tmp_block_buffer);
    kfree(inode);
    return read_size;
}

static int write(mounting_point* mp, uint32_t inode_id, uint32_t offset, const char* buffer, uint32_t size) {
    ext2_data* data = (ext2_data*)mp->data;
    const size_t block_size = data->dev->block_size; 
    ext2_inode inode;
    if (get_inode_by_id(data, inode_id, &inode) < 0) {
        return -1;
    }

    uint32_t begin_block = offset / block_size;
    uint32_t cur_offset = offset % block_size;
    uint32_t end_block = (offset + size + block_size - 1) / block_size;
    int written = 0;
    for (int i = begin_block; i < end_block; ++i) {
        int phys_block;
        if ((phys_block = get_phys_block_no_in_inode(data, &inode, i)) == 0) {
            phys_block = block_alloc(data);
            if (phys_block < 0) return -1;
            insert_block_in_inode(data, &inode, phys_block, i);
        }
        auto cur_block = get_cache_ptr(*data->cache_data, phys_block);
        size_t write_size = (block_size - cur_offset) < size ? (block_size - cur_offset) : size;
        memcpy(cur_block.get() + cur_offset, buffer, write_size);
        taint(*data->cache_data, phys_block);
        buffer += write_size;
        size -= write_size;
        written += write_size;
        cur_offset = 0;
    }
    uint32_t new_end = offset + written;
    if (new_end > inode.i_size) {
        inode.i_size = new_end;
    }
        
    set_inode_by_id(data, inode_id, &inode);
    flush_all_block(*data->cache_data);
    flush_metadata(data);
    return written;
}

static int remove_entry_from_dir(ext2_data* data, uint32_t dir_inode_no, uint32_t entry_inode_no) {
    ext2_inode dir_inode;
    if (get_inode_by_id(data, dir_inode_no, &dir_inode) < 0) {
        return -1;
    }

    const size_t block_size = data->dev->block_size; 

    uint32_t block_num = (dir_inode.i_size + block_size - 1) / block_size;
    for (int i = 0; i < block_num; ++i) {
        uint32_t read_offset = 0;
        uint32_t last_offset = 0;
        int phys_block_no = get_phys_block_no_in_inode(data, &dir_inode, i);
        auto cache_ptr = get_cache_ptr(*data->cache_data, phys_block_no);
        while (read_offset < block_size) {
            ext2_dir_entry* entry = reinterpret_cast<ext2_dir_entry*>(cache_ptr.get() + read_offset);
            if (entry->rec_len == 0) break;
            if (entry->inode == entry_inode_no) {
                if (last_offset == read_offset) { // 我就是第一项
                    entry->inode = 0;  // 标记为已删除，保留空间
                    taint(*data->cache_data, phys_block_no);
                    return 0;
                }
                ext2_dir_entry* last_entry = reinterpret_cast<ext2_dir_entry*>(cache_ptr.get() + last_offset);
                last_entry->rec_len += entry->rec_len;
                taint(*data->cache_data, phys_block_no);
                return 0;
            }
            last_offset = read_offset;
            read_offset += entry->rec_len;
        }
    }

    return -1; // 找不到对应条目
}

static void free_block_tree(ext2_data* data, uint32_t inode_no, int depth) {
    if (inode_no == 0) return;
    if (depth > 0) {
        auto cache_ptr = get_cache_ptr(*data->cache_data, inode_no);
        for (int i = 0; i < data->dev->block_size / 4; ++i) {
            free_block_tree(data, *(reinterpret_cast<uint32_t*>(cache_ptr.get()) + i), depth - 1);
        }
    }
    block_free(data, inode_no);
}

static int ext2_unlink(mounting_point* mp, uint32_t inode_id) {
    ext2_data* data = (ext2_data*)mp->data;
    const size_t block_size = data->dev->block_size; 
    ext2_inode inode;
    if (get_inode_by_id(data, inode_id, &inode) < 0) {
        return -1;
    }

    if (--inode.i_links_count != 0) {
        set_inode_by_id(data, inode_id, &inode);
        return 0;
    }
    for (int i = 0; i < 12; ++i) free_block_tree(data, inode.i_block[i], 0);
    free_block_tree(data, inode.i_block[12], 1);
    free_block_tree(data, inode.i_block[13], 2);
    free_block_tree(data, inode.i_block[14], 3);
    memset(&inode, 0, sizeof(inode));
    set_inode_by_id(data, inode_id, &inode);
    inode_free(data, inode_id);
    
    return 0;
}

static int unlink(mounting_point* mp, const char* path) {
    ext2_data* data = (ext2_data*)mp->data;
    const size_t block_size = data->dev->block_size; 
    int inode_id = relative_lookup(data, ROOT_INODE_NO, path);
    if (inode_id <= 0) {
        return -1;
    }
    char dir_path[256];
    char name[256];
    split_path(path, dir_path, name);
    int dir_inode_id = relative_lookup(data, ROOT_INODE_NO, dir_path);
    remove_entry_from_dir(data, dir_inode_id, inode_id);
    ext2_unlink(mp, inode_id);
    flush_all_block(*data->cache_data);
    flush_metadata(data);
    return 0;
}

static int mkdir(mounting_point* mp, const char* path) {
    if (strlen(path) > 255) return -1;
    char make_path[256];
    strcpy(make_path, path);
    if (make_path[strlen(make_path) - 1] == '/') make_path[strlen(make_path) - 1] = '\0';
    char dir_path[256];
    char name[256];
    split_path(make_path, dir_path, name);
    if (strlen(name) == 0) return -1;
    if (strcmp(name, "..") == 0 || strcmp(name, ".") == 0) return -1; // 不合法的名字
    ext2_data* data = (ext2_data*)mp->data;
    int dir_inode_id = relative_lookup(data, ROOT_INODE_NO, dir_path);
    if (dir_inode_id <= 0) {
        return -1;
    }
    int new_inode_id = inode_alloc(data);
    if (new_inode_id < 0) return -1;
    ext2_inode inode;
    init_inode(inode, FILE_TYPE_DIR);
    add_entry_to_dir(data, dir_inode_id, new_inode_id, FILE_TYPE_DIR, name);
    set_inode_by_id(data, new_inode_id, &inode);
    add_entry_to_dir(data, new_inode_id, new_inode_id, FILE_TYPE_DIR, ".");
    add_entry_to_dir(data, new_inode_id, dir_inode_id, FILE_TYPE_DIR, "..");

    ext2_inode par_inode;
    get_inode_by_id(data, dir_inode_id, &par_inode);
    ++par_inode.i_links_count;
    set_inode_by_id(data, dir_inode_id, &par_inode);

    flush_all_block(*data->cache_data);
    flush_metadata(data);
    return 0;
}

static int close(mounting_point* mp, uint32_t inode_id, uint32_t mode) {
    return 0;
}

constexpr uint32_t MODE_FTYPE_DIR = 4;

static int stat(mounting_point* mp, const char* path, file_stat* out) {
    ext2_data* data = (ext2_data*)mp->data;
    if (strcmp(path, "/") == 0) {
        out->group_id = (data->root_inode.i_gid_high << 16) | data->root_inode.i_gid;
        out->owner_id = (data->root_inode.i_uid_high << 16) | data->root_inode.i_uid;
        out->size = data->root_inode.i_size;
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

static int opendir(mounting_point* mp, const char* path) {
    ext2_data* data = (ext2_data*)mp->data;
    int inode_no = relative_lookup(data, ROOT_INODE_NO, path);
    return inode_no == 0 ? -1 : inode_no;
}

static int readdir(mounting_point* mp, uint32_t inode_id, uint32_t offset, dirent* out) {
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

static int closedir(mounting_point* mp, uint32_t inode_id) {
    return 0;
}

static int ioctl(mounting_point* mp, uint32_t inode_id, uint32_t request, void* arg) {
    return -1;
}

static int peek(mounting_point* mp, uint32_t inode_id) {
    if (inode_id == 0) return -1;
    ext2_data* data = (ext2_data*)mp->data;
    const size_t block_size = data->dev->block_size;
    ext2_inode* inode = static_cast<ext2_inode*>(kmalloc(sizeof(ext2_inode)));
    get_inode_by_id(data, inode_id, inode);
    int size = inode->i_size;
    kfree(inode);
    return size;
}

static int set_poll(mounting_point* mp, uint32_t inode_id, process_queue* poll_queue) {
    return -1;
}

void init_ext2fs() {
    ext2_fs_operation.mount    = &mount;
    ext2_fs_operation.unmount  = &unmount;
    ext2_fs_operation.open     = &open;
    ext2_fs_operation.read     = &read;
    ext2_fs_operation.write    = &write;
    ext2_fs_operation.close    = &close;
    ext2_fs_operation.opendir  = &opendir;
    ext2_fs_operation.readdir  = &readdir;
    ext2_fs_operation.closedir = &closedir;
    ext2_fs_operation.stat     = &stat;
    ext2_fs_operation.ioctl    = &ioctl;
    ext2_fs_operation.set_poll = &set_poll;
    ext2_fs_operation.peek     = &peek;
    ext2_fs_operation.unlink   = &unlink;
    ext2_fs_operation.mkdir    = &mkdir;
    ext2_fs_operation.truncate    = &truncate;
    ext2_fs_operation.rename   = nullptr;
    ext2_fs_operation.sock_opr = nullptr;
    register_fs_operation(FS_DRIVER::EXT2FS, &ext2_fs_operation); // 先让ext2挂载失败
}