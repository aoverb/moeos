## Homemade OS (16): tarfs

```
This article is incomplete... under construction.
```

In the previous chapter, we discussed the abstract VFS. In this chapter, we'll talk about the concrete tarfs.

### tar Files

The tar file format was originally used to organize files on magnetic tape. Parsing this format is simple: since files are recorded sequentially, we can read everything out in a single loop. We parse in 512-byte units, reading a header, getting the size, then skipping 512 + size bytes to reach the next header — until we encounter an empty file name.

Note that many fields are recorded as octal strings.

#### Building an Index

Since sequential reading is time-consuming, we can build a tree index based on the tar headers during mount time. To create key-value pairs mapping file names to inode IDs, I had Claude help me implement an `unordered_map`.

Our shell parsed successfully and executed correctly.

![image-20260228174157600](../assets/自制操作系统（16）：tarfs/image-20260228174157600.png)
