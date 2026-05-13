## Homemade OS (32): Ext2 Filesystem Driver — Ext2 Mount, Superblock Parsing

In the previous chapter, we directly passed the block device data to the mount point. That's not enough — we need one more layer of abstraction:

```cpp
struct ext2_data {
    block_device* dev;
    ext2_super_block sb;
};
```

Then we mount with this wrapper:

```cpp
        if (bdev_list[i]->fs == file_system::EXT2) {
            ext2_data* data = (ext2_data*)kmalloc(sizeof(ext2_data));
            data->dev = bdev_list[i];
            mounting_point* ret = v_mount(FS_DRIVER::EXT2FS, "/ext2", data);
```

The reason we extract `ext2_super_block` separately is that the superblock is a frequently accessed data structure, so we want to cache it in memory.

In `v_mount`, we read the superblock and copy it to `sb`:

```cpp
// Read the superblock — it's located at offset 1024 from block 0
uint32_t sb_block_num = 1024 / data->dev->block_size;
uint32_t sb_offset = 1024 % data->dev->block_size;
// Get block size
void* buffer = kmalloc(data->dev->block_size);
int ret = data->dev->read(data->dev, sb_block_num, buffer);
if (ret < 0) {
    kfree(buffer);
    return -1; // Failed to read superblock
}
ext2_super_block* ext2_sb = reinterpret_cast<ext2_super_block*>((char*)buffer + sb_offset);
data->sb = *ext2_sb;
kfree(buffer);
```

**!! One very important point: the "block" of a superblock and the "block" of "block size" are NOT the same concept. The former is a logical concept (a data structure), not equivalent to the latter's physical block concept.**

#### File Organization

In the ext2 filesystem, an inode represents a file. Think of it this way: a block device stores files as small blocks scattered across the disk like beads, and the inode is the string that threads these beads into a necklace. Moreover, a file consists not only of its data but also its **metadata**: filename, creation date, permissions, owner, etc.

![image-20260315145048374](../assets/自制操作系统（32）：Ext2文件系统驱动——Ext2挂载，超级块解析/image-20260315145048374.png)

The upper portion of the diagram shows the metadata I mentioned, while the lower portion is an array of pointers to the data blocks constituting the file. This array has only 15 elements. If a data block is 4KB, does that mean a file can be at most 60KB? No! Only the first 12 entries directly point to data blocks. The remaining three entries point to pointer blocks, called **single, double, and triple indirect pointers**:

![image-20260315145551656](../assets/自制操作系统（32）：Ext2文件系统驱动——Ext2挂载，超级块解析/image-20260315145551656.png)

Also, the "necklaces" themselves need to be organized together — that's the role of **directories**. A directory is itself a type of file, but its data blocks record the inode indices of the files and subdirectories it contains:

![image-20260315145726825](../assets/自制操作系统（32）：Ext2文件系统驱动——Ext2挂载，超级块解析/image-20260315145726825.png)

With this "directory-contains-directory, directory-contains-file" structure, the entire filesystem's data is organized like a tree. So to find any file in the filesystem, we first find the root directory and traverse downward. How do we find the "root directory"? EXT2 specifies that **inode number 2** is the root directory inode.

```
Further reading: Special inodes:

ext2 reserves the first few inodes as follows:

inode 0  — unused, indicates "invalid/empty"
inode 1  — bad blocks inode, records bad sectors on disk
inode 2  — root directory /, the starting point of the entire directory tree
inode 3  — ACL index (early design, mostly unused)
inode 4  — ACL data (same as above)
inode 5  — boot loader inode
inode 6  — undelete directory
inode 7–10 — reserved

Starting from inode 11, regular user files and directories can be allocated. The superblock's s_first_ino field records the first non-reserved inode number, which defaults to 11 in ext2.
```

So we just need to find inode 2... But wait, where is inode 2 stored? This is a block device — in other words, which block contains this inode, and at what offset within that block?

Imagine an array called `inode_table`, where index 0 points to inode 1 (since inode 0 is invalid, we start counting from 1), index 1 points to inode 2, and so on. The superblock tells us the inode size and that `inode_table` starts at some block `k`. To find inode 2:

```
idx = node_id - 1;
inodes_count_in_each_block = block_size / inode_size;
```

The block is: `k + floor(idx / inodes_count_in_each_block)`, and the offset within that block is: `idx % inodes_count_in_each_block`.

Pseudo-code:

```cpp
inode* get_inode_by_id (uint32_t id) {
    if (id == 0) return nullptr;
    uint32_t inode_table_block_id = sb.inode_table; // Starting block of inode_table

    size_t inode_size = sb.inode_size;
    size_t block_size = sb.block_size;
    uint32_t inodes_count_in_each_block = block_size / inode_size;

    uint32_t idx = id - 1;
    uint32_t block_idx = idx / inodes_count_in_each_block;
    uint32_t offset = idx % inodes_count_in_each_block;

    void* buffer = kmalloc(block_size);
    read_block(inode_table_block_id + block_idx,  buffer);

    return (static_cast<inode*>(buffer) + offset);
}
```

But is the `inode_table` start block address simply stored in the superblock? The filesystem designers thought one step further.

Back in the era of mechanical hard drives and floppy disks, finding files on such storage had a physical cost — switching heads, seeking cylinders, seeking tracks. There was overhead from the read head moving back and forth. The designers thought: if the `inode_table` were just a series of blocks at the logical start of the disk, then for files farther away from these blocks, if a user wanted to open files sequentially (which is very likely — users tend to access nearby files within a short period), the back-and-forth overhead would be significant. (It's like visiting relatives in a city in Guangdong, but having to go back to Beijing to find out where they live — not pleasant!)

Considering this, the designers organized disk blocks into **block groups**. Each block group consists of logically contiguous blocks of equal size (the last group may have fewer, but no inode will point to non-existent blocks, so it doesn't matter):

![image-20260315163656144](../assets/自制操作系统（32）：Ext2文件系统驱动——Ext2挂载，超级块解析/image-20260315163656144.png)

At the beginning of each block group is a **group descriptor**, which contains information about the group and its blocks — including the `bg_inode_table` we mentioned earlier:

![image-20260315164008469](../assets/自制操作系统（32）：Ext2文件系统驱动——Ext2挂载，超级块解析/image-20260315164008469.png)

Note: Figure 14-2 has an issue. It draws the boot block outside block group 0, as if it's independent of the block group system. But actually:

- When the block size is 1024, the boot block occupies block 0, and block group 0 starts at block 1 (superblock). In this case, you could say the boot block is "outside" block group 0.
- But when the block size is 2048 or 4096, block group 0 starts at logical block 0. The boot block's 1024 bytes are inside block group 0's block 0, and the superblock is also inside it. In that case, the boot block is part of block group 0, not independent.

So this diagram only applies when the block size is exactly 1024, but it's drawn as if it's a universal layout, which can be misleading.

Notice that each block group has a superblock at its beginning — it's so important that every group stores a copy as backup. Each group also has a **group descriptor table** containing descriptors for all groups, since it's also critical.

The superblock stores how many inodes each block group contains; the group descriptor table tells us each group's `bg_inode_table`. So we can find an inode like this:

(Pseudo-code)

```cpp
inode* get_inode_by_id (uint32_t id) {
    if (id == 0) return nullptr;
    uint32_t inodes_per_group = sb.s_inodes_per_group;
    // First, figure out which group I'm in
    uint32_t idx = id - 1;
    
    uint32_t group_idx = idx / inodes_per_group;
    
    // We'll discuss how to get the descriptor for a group index later
    group_description* gd = get_group_description_by_group_idx(group_idx);
    
    uint32_t inode_table_block_id = gd.bg_inode_table; // Starting block of bg_inode_table

    size_t inode_size = sb.inode_size;
    size_t block_size = sb.block_size;
    uint32_t inodes_count_in_each_block = block_size / inode_size;

    // We also need to compute the relative ID within this block group
    uint32_t idx_in_group = idx % inodes_per_group;

    uint32_t block_idx = idx_in_group / inodes_count_in_each_block;
    uint32_t offset = idx_in_group % inodes_count_in_each_block;

    void* buffer = kmalloc(block_size);
    read_block(inode_table_block_id + block_idx,  buffer);

    return (static_cast<inode*>(buffer) + offset);
}
```

For `get_group_description_by_group_idx`: finding a descriptor in the descriptor table is straightforward once you know the starting block of the descriptor table (right after the superblock) and the size of each group descriptor. It's another round of block number and offset calculation, which I won't repeat here.

#### Back to Mounting...

Oops — I almost forgot we were talking about mounting. Why did I go off on such a long tangent about file organization?

Mounting is essentially two things: **validating data** and **caching frequently used, rarely modified hot data** so we don't have to read from I/O ports every time. After the introduction above, you probably have a good idea of what to cache. For example, the most obvious candidates are the `bg_inode_table` entries in the group descriptor table — something that's frequently read and rarely modified. With this background knowledge, you know why and how to cache them in memory during mounting.

At the end of mounting, it's best to also read the root directory's inode into memory, since it also changes infrequently and is accessed often.

```cpp
static int get_inode_by_id (ext2_data* data, uint32_t id, ext2_inode* out_inode) {
    if (id == 0) return -1;
    ext2_super_block& sb = data->sb;
    uint32_t inodes_per_group = sb.s_inodes_per_group;
    // First, figure out which group I'm in
    uint32_t idx = id - 1;
    
    uint32_t group_idx = idx / inodes_per_group;
    
    ext2_group_desc& gd = data->gdt[group_idx];
    
    uint32_t inode_table_block_id = gd.bg_inode_table; // Starting block of bg_inode_table

    size_t inode_size = sizeof(ext2_inode);
    size_t block_size = data->dev->block_size;
    uint32_t inodes_count_in_each_block = block_size / inode_size;

    // We also need to compute the relative ID within this block group
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
    // A journey of a thousand miles begins with a single step...
    ext2_data* data = (ext2_data*)mp->data;

    // Read the superblock at offset 1024 from block 0
    uint32_t sb_block_num = 1024 / data->dev->block_size;
    uint32_t sb_offset = 1024 % data->dev->block_size;
    void* buffer = kmalloc(data->dev->block_size);
    int ret = data->dev->read(data->dev, sb_block_num, buffer);
    if (ret < 0) {
        kfree(buffer);
        return -1; // Failed to read superblock
    }
    ext2_super_block* ext2_sb = reinterpret_cast<ext2_super_block*>((char*)buffer + sb_offset);
    data->sb = *ext2_sb;
    kfree(buffer);

    ext2_super_block& sb = data->sb;

    // Check magic number...
    if (sb.s_magic != 0xEF53) {
        return -1;
    }
    // Check disk state (optional)
    // sb.s_state...

    // Cache the group descriptor table
    data->bg_num = (sb.s_blocks_count + sb.s_blocks_per_group - 1) / sb.s_blocks_per_group;
    data->gdt = (ext2_group_desc*)kmalloc(sizeof(ext2_group_desc) * data->bg_num);

    // How many blocks needed to read all group descriptors?
    uint32_t bg_block_num = (data->bg_num * sizeof(ext2_group_desc) + data->dev->block_size - 1) / data->dev->block_size;

    uint32_t gd_idx = 0;

    // One block can hold data->dev->block_size / sizeof(ext2_group_desc) descriptors
    uint32_t gd_per_block = (data->dev->block_size / sizeof(ext2_group_desc));
    for (int blk_idx = 0; blk_idx < bg_block_num; ++blk_idx) {
        void* gd_buffer = kmalloc(data->dev->block_size);
        data->dev->read(data->dev, sb_block_num + 1 + blk_idx, gd_buffer); // GDT block is right after the superblock block
        
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
```

---

In the next chapter, we'll discuss inode parsing and file opening.
