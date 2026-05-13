## Homemade OS (31): Ext2 Filesystem Driver — ATA PIO Sector Read/Write, Block Device Abstraction

We're tired of the read-only world. It's time to enter a world where we can mount a writable filesystem!

I chose EXT2 because it's close to Linux, not too simple but not too hard, shares many similarities with EXT4, and is well-suited for deepening our understanding of modern filesystems.

First, let's look at how to identify a device and read a specific sector from it.

### Creating and Mounting an Image

Let's create a 4MB image, format it as EXT2, mount it to our system, copy the SYSROOT contents into it, then unmount:

```shell
aoverb@BA:~/lolios$ dd if=/dev/zero of=disk.img bs=4M count=32
32+0 records in
32+0 records out
134217728 bytes (134 MB, 128 MiB) copied, 0.23078 s, 582 MB/s
aoverb@BA:~/lolios$ mkfs.ext2 disk.img
mke2fs 1.47.0 (5-Feb-2023)
Discarding device blocks: done
Creating filesystem with 32768 4k blocks and 32768 inodes

Allocating group tables: done
Writing inode tables: done
Writing superblocks and filesystem accounting information: done

aoverb@BA:~/lolios$ mkdir /tmp/mnt && sudo mount disk.img /tmp/mnt
[sudo] password for aoverb:
aoverb@BA:~/lolios$ cd SYSROOT/
aoverb@BA:~/lolios/SYSROOT$ sudo cp -rp * /tmp/mnt/
aoverb@BA:~/lolios$ sudo umount /tmp/mnt
```

**Note: We're not discussing disk partitions or partition tables right now, so our image doesn't have one. We're focusing on the Ext2 filesystem itself; partition tables can come later.**

Then when starting QEMU, we can use the `-drive` parameter to specify the image:

```shell
qemu-system-i386 -cdrom lolios.iso -drive file=disk.img,format=raw
```

### ATA PIO

ATA PIO is a method for driving disk drives. ATA is an interface standard for disk drives. PIO stands for Programmed Input/Output — "programmed" means the CPU is involved throughout the entire process, and "I/O" means we send commands to the disk controller and read data through I/O ports via the CPU. Since there's no DMA, it keeps the CPU busy while reading. However, its advantage is simple implementation — we can use the `in*/out*` I/O read/write functions we've used before to check disk device status and read/write. We're still in the early implementation phase, so this is perfectly fine to use. Later, if performance becomes a bottleneck, we can adopt approaches like PCI MMIO for reading disk sectors.

#### LBA

LBA stands for Logical Block Address. It treats the entire disk as a linear array, where we can specify a sector by directly indexing into it. This replaces the old CHS (Cylinder, Head, Sector) indexing scheme.

#### Reading Disk Status

We can use a series of commands to check if an ATA block device is attached to the master:

```cpp
#include <kernel/io.h>
#include <stdio.h>
#include <driver/ata.hpp>

constexpr uint16_t REG_DATA        = 0x1F0;
constexpr uint16_t REG_SECTOR_COUNT = 0x1F2;
constexpr uint16_t REG_LBA_LOW     = 0x1F3;
constexpr uint16_t REG_LBA_MID     = 0x1F4;
constexpr uint16_t REG_LBA_HIGH    = 0x1F5;
constexpr uint16_t REG_DRIVE_SELECT = 0x1F6;
constexpr uint16_t REG_STAT_CMD    = 0x1F7;

constexpr uint8_t CMD_IDENTIFY = 0xEC;

constexpr uint8_t DRIVE_MASTER = 0xA0;

constexpr uint8_t STAT_BUSY = 7;

void ata_init() {
    // Detect ATA device — checking the primary bus master is sufficient
    outb(REG_DRIVE_SELECT, DRIVE_MASTER);
    outb(REG_SECTOR_COUNT, 0);
    outb(REG_LBA_LOW, 0);
    outb(REG_LBA_MID, 0);
    outb(REG_LBA_HIGH, 0);
    outb(REG_STAT_CMD, CMD_IDENTIFY);

    uint8_t master_stat = inb(REG_STAT_CMD);

    if (master_stat == 0) { // No device
        return;
    }
    while((inb(REG_STAT_CMD) & (1 << STAT_BUSY)));


    uint8_t lba_mid = inb(REG_LBA_MID);
    uint8_t lba_high = inb(REG_LBA_HIGH);

    // Ignore other device types
    if (lba_mid != 0 || lba_high != 0) {
        return;
    }
    // Must read IDENTIFY data or device status will be incorrect
    uint16_t data;
    for (int i = 0; i < 256; ++i) {
        data = inw(REG_DATA);
    }
    // todo, register block device
    return;
}
```

By sending the `CMD_IDENTIFY` command, we can check the status register to see if a device is present. Then, after polling the status register to confirm device info is ready, we check if the LBA mid and high register values are zero to confirm the master has an ATA block device:

![image-20260314182956316](../assets/自制操作系统（31）：Ext2文件系统驱动——ATA PIO驱动读写扇区，块设备抽象/image-20260314182956316.png)

Our system can now detect ATA devices attached to the master.

#### Reading/Writing Specific Sectors

To read or write a specific sector on an ATA disk, we need to specify three things:

1. Which device (master or slave)
2. Starting logical sector (LBA)
3. How many sectors to read consecutively

```cpp
#include <kernel/io.h>
#include <driver/ata.hpp>
#include <stdio.h>
#include <string.h>

constexpr uint16_t REG_DATA        = 0x1F0;
constexpr uint16_t REG_SECTOR_COUNT = 0x1F2;
constexpr uint16_t REG_LBA_LOW     = 0x1F3;
constexpr uint16_t REG_LBA_MID     = 0x1F4;
constexpr uint16_t REG_LBA_HIGH    = 0x1F5;
constexpr uint16_t REG_DRIVE_SELECT = 0x1F6;
constexpr uint16_t REG_STAT_CMD    = 0x1F7;

constexpr uint8_t CMD_READ_SECTOR  = 0x20;
constexpr uint8_t CMD_WRITE_SECTOR = 0x30;
constexpr uint8_t CMD_IDENTIFY     = 0xEC;

constexpr uint8_t DRIVE_MASTER = 0xE0; // master select + LBA mode

constexpr uint8_t STAT_ERR  = 0;
constexpr uint8_t STAT_DRQ  = 3; // Data Request
constexpr uint8_t STAT_DF   = 5; // Drive Fault
constexpr uint8_t STAT_BUSY = 7;

...

int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buffer) {
    if (drive != DRIVE_MASTER) return -1; // Only master device — being lazy...
    // Wait for device idle
    while((inb(REG_STAT_CMD) & (1 << STAT_BUSY)));
    // Select device
    outb(REG_DRIVE_SELECT, DRIVE_MASTER | ((lba >> 24) & 0xF));
    // Set sector count
    outb(REG_SECTOR_COUNT, count);
    // Set LBA
    outb(REG_LBA_LOW,   (lba & 0xFF));
    outb(REG_LBA_MID,   ((lba >> 8) & 0xFF));
    outb(REG_LBA_HIGH,  ((lba >> 16) & 0xFF));
    // Send read command
    outb(REG_STAT_CMD, CMD_READ_SECTOR);
    uint16_t cnt = count == 0 ? 256 : count;
    for (int s = 0; s < cnt; ++s) {
        // Check at sector boundaries
        uint8_t status;
        while ((status = inb(REG_STAT_CMD)) & (1 << STAT_BUSY));
        if (!(status & (1 << STAT_DRQ)) || (status & ((1 << STAT_DF) | (1 << STAT_ERR)))) {
            // Error occurred, return immediately
            return -1;
        }
        // 512 bytes per sector, 2 bytes per data register read, so 256 reads per sector
        for (int i = 0; i < 256; ++i) {
            *(reinterpret_cast<uint16_t*>(buffer) + i + 256 * s) = inw(REG_DATA);
        }
    }
    return 0;
}

int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void* buffer) {
    if (drive != DRIVE_MASTER) return -1; // Only master device — being lazy...
    // Wait for device idle
    while((inb(REG_STAT_CMD) & (1 << STAT_BUSY)));
    // Select device
    outb(REG_DRIVE_SELECT, DRIVE_MASTER | ((lba >> 24) & 0xF));
    // Set sector count
    outb(REG_SECTOR_COUNT, count);
    // Set LBA
    outb(REG_LBA_LOW,   (lba & 0xFF));
    outb(REG_LBA_MID,   ((lba >> 8) & 0xFF));
    outb(REG_LBA_HIGH,  ((lba >> 16) & 0xFF));
    // Send write command
    outb(REG_STAT_CMD, CMD_WRITE_SECTOR);
    uint16_t cnt = count == 0 ? 256 : count;
    for (int s = 0; s < cnt; ++s) {
        // Check at sector boundaries
        uint8_t status;
        while ((status = inb(REG_STAT_CMD)) & (1 << STAT_BUSY));
        if (!(status & (1 << STAT_DRQ)) || (status & ((1 << STAT_DF) | (1 << STAT_ERR)))) {
            // Error occurred, return immediately
            return -1;
        }
        // 512 bytes per sector, 2 bytes per data register write, so 256 writes per sector
        for (int i = 0; i < 256; ++i) {
            outw(REG_DATA, *(reinterpret_cast<const uint16_t*>(buffer) + i + 256 * s));
        }
    }
    // Flush cache to ensure write data takes effect
    outb(REG_STAT_CMD, CMD_FLUSH_CACHE);
    while (inb(REG_STAT_CMD) & (1 << STAT_BUSY));
    return 0;
}
```

### Registering Block Devices

First, let's define a block device structure:

```cpp
struct block_device {
    uint8_t device_id;
    void* data;

    int (*read)(block_device* dev, uint32_t lba, uint8_t cnt, void* buffer);
    int (*write)(block_device* dev, uint32_t lba, uint8_t cnt, const void* buffer);
};
```

Define a registration function:

```cpp
#include <driver/block.hpp>

constexpr size_t MAX_BLOCK_DEVICE_NUM = 256;
static block_device* bdev_list[MAX_BLOCK_DEVICE_NUM];
static size_t bdev_cnt = 0;

int register_block_device(block_device* bd) {
    if (bdev_cnt >= MAX_BLOCK_DEVICE_NUM) return -1;
    bdev_list[++bdev_cnt] = bd;
    return bdev_cnt;
}
```

Then at the end of `init`, we construct this structure:

```cpp
    // Must read IDENTIFY data or device status will be incorrect
    uint16_t identify[256];
    for (int i = 0; i < 256; ++i) {
        identify[i] = inw(REG_DATA);
    }

    uint32_t total_sectors = (uint32_t)identify[61] << 16 | identify[60];

    block_device* bd = (block_device*)kmalloc(sizeof(block_device));

    bd->data = nullptr;
    bd->device_id = DRIVE_MASTER;
    bd->total_sectors = total_sectors;
    bd->sector_size = 512;
    bd->block_size = 0; // We'll only know this after mounting
    // bd->read = ;

    int ret = register_block_device(bd);
    if (ret == -1) {
        printf("Failed to register master block device!");
    }
    return;
```

#### Mounting the Registered Block Device

Let's first introduce the concept of the **superblock** in ext2.

![image-20260315001920506](../assets/自制操作系统（31）：Ext2文件系统驱动——ATA PIO驱动读写扇区，块设备抽象/image-20260315001920506.png)

The image above shows a structure diagram. We'll focus on the "magic number" field — we'll use it to determine if our block device's filesystem is EXT2.

```cpp
void block_init() {
    for (int i = 0; i < bdev_cnt; ++i) {
        block_device* bd = bdev_list[i];

        // We're treating this directly as EXT2 for now — this is technically wrong...
        // If we need to support FAT later, we should check for FAT first
        bd->block_size = SUPERBLOCK_SIZE; // Set block size to 1024 to read superblock contents
        void* buffer = kmalloc(SUPERBLOCK_SIZE);
        bd->read(bd, 1, buffer);
        // Not EXT2, skip initialization
        ext2_super_block* ext2_sb = reinterpret_cast<ext2_super_block*>(buffer);
        if (ext2_sb->s_magic != 0xEF53) {
            bd->fs = file_system::UNKNOWN; // Mark unknown block devices
            kfree(buffer);
            continue;
        }
        bd->block_size = (1 << ext2_sb->s_log_block_size) * 1024;
        bd->fs = file_system::EXT2;
    }
}
```

Then we can initialize our ext2fs driver:

```cpp
void fs_init(saved_module* saved, uint32_t mod_count) {
    printf("filesystem initializing...\n");
    init_vfs();
    init_tarfs();
    init_devfs();
    init_pipefs();
    init_sockfs();
    init_ext2fs();

    ...

    // For now we know we only have one disk, so we can force mount it
    // Later with multiple disks we'll need to adapt
    for (int i = 0; i < bdev_cnt; ++i) {
        if (bdev_list[i]->fs == file_system::EXT2) {
            mounting_point* ret = v_mount(FS_DRIVER::EXT2FS, "/ext2", bdev_list[i]);
            if (ret == nullptr) {
                printf("failed to mount ext2 block device to /ext2!\n");
            } else {
                printf("Root mounted!\n");
            }
            break;
        }
    }
```

```cpp
fs_operation ext2_fs_operation;

static int mount(mounting_point* mp) {
    // A journey of a thousand miles begins with a single step...
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
    ext2_fs_operation.stat     = nullptr;
    ext2_fs_operation.ioctl    = nullptr;
    ext2_fs_operation.set_poll = nullptr;
    ext2_fs_operation.peek     = nullptr;
    ext2_fs_operation.sock_opr = nullptr;
    register_fs_operation(FS_DRIVER::EXT2FS, &ext2_fs_operation);
}
```

![image-20260315010409325](../assets/自制操作系统（31）：Ext2文件系统驱动——ATA PIO驱动读写扇区，块设备抽象/image-20260315010409325.png)

It shows "mount failed" for now, but everything starts from here...

---

In the next chapter, we'll implement ext2's `mount`!
