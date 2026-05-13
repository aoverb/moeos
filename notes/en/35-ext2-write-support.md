## Homemade OS (35): Ext2 Filesystem Driver — Write Support

```
This article is not complete... under construction.
```

With the inode allocator, we can find new space for writing, but the contents (i.e., the inode itself) still need a new function to initialize.

An inode consists of two parts: **metadata** and **data blocks**.

Let's first implement a function to update inode metadata:

#### set_inode_by_id

This is essentially a modified version of `get_inode_by_id` — change the specified inode's content in the buffer and mark the cache as dirty.

Now let's implement a function to append a data block to a specified inode:

#### static size_t append_block_in_inode(ext2_data\* data, ext2_inode\* inode, uint32_t block_no)

Initially I wanted to write an append block function, but then I discovered there's a type of file called a **sparse file**... ugh.

#### // Insert a specified physical block at insert_idx; if a block already exists, return -1
```cpp
static size_t insert_block_in_inode(ext2_data* data, ext2_inode* inode,
    uint32_t block_no, uint32_t insert_idx)
```

The idea is similar to `read_block_in_inode`, but simpler. Find which pointer level the insertion position falls into, and handle each case separately.

The code is long and tedious...

#### Directory Entry Insertion

```cpp
struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;   // 1=regular file, 2=directory, 7=symlink ...
    char     name[];      // NOT null-terminated!
};
```

This is just about filling in the fields and inserting them into an inode's data blocks.

Actually, we need a function to get the physical block corresponding to a specified logical block in an inode.

#### open

Now we can implement `open`.

The code...

![image-20260318015312712](../assets/自制操作系统（35）：Ext2文件系统驱动——写入支持/image-20260318015312712.png)

Strange behavior...

Turns out it was a `char` issue.

#### Result

![image-20260318025555967](../assets/自制操作系统（35）：Ext2文件系统驱动——写入支持/image-20260318025555967.png)

Successfully implemented a writable Ext2 driver!

![image-20260318025728799](../assets/自制操作系统（35）：Ext2文件系统驱动——写入支持/image-20260318025728799.png)

#### unlink, mkdir

---

Our Ext2 filesystem journey comes to an end here. In the next chapter, let's port a text editor — **kilo**.
