#include <kernel/io.h>
#include <kernel/mm.hpp>
#include <driver/ata.hpp>
#include <driver/block.hpp>
#include <stdio.h>
#include <string.h>

constexpr uint16_t REG_DATA = 0x1F0;
constexpr uint16_t REG_SECTOR_COUNT = 0x1F2;
constexpr uint16_t REG_LBA_LOW = 0x1F3;
constexpr uint16_t REG_LBA_MID = 0x1F4;
constexpr uint16_t REG_LBA_HIGH = 0x1F5;
constexpr uint16_t REG_DRIVE_SELECT = 0x1F6;
constexpr uint16_t REG_STAT_CMD = 0x1F7;

constexpr uint8_t CMD_READ_SECTOR = 0x20;
constexpr uint8_t CMD_WRITE_SECTOR = 0x30;
constexpr uint8_t CMD_IDENTIFY = 0xEC;
constexpr uint8_t CMD_FLUSH_CACHE = 0xE7;

constexpr uint8_t DRIVE_MASTER = 0xE0; // master选择 + LBA模式

constexpr uint8_t STAT_ERR = 0;
constexpr uint8_t STAT_DRQ = 3; // Dara Request
constexpr uint8_t STAT_DF = 5; //  Drive Fault
constexpr uint8_t STAT_BUSY = 7;


int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buffer) {
    if (drive != DRIVE_MASTER) return -1; // 只有主设备，就偷懒了...
    // 等待设备空闲
    while((inb(REG_STAT_CMD) & (1 << STAT_BUSY)));
    // 选择设备
    outb(REG_DRIVE_SELECT, DRIVE_MASTER | ((lba >> 24) & 0xF));
    // 填入需要读取的扇区数
    outb(REG_SECTOR_COUNT, count);
    // 填入LBA
    outb(REG_LBA_LOW, (lba & 0xFF));
    outb(REG_LBA_MID, ((lba >> 8) & 0xFF));
    outb(REG_LBA_HIGH, ((lba >> 16) & 0xFF));
    // 发送读命令
    outb(REG_STAT_CMD, CMD_READ_SECTOR);
    uint16_t cnt = count == 0 ? 256 : count;
    for (int s = 0; s < cnt; ++s) {
        // 在扇区的边界执行检查即可
        uint8_t status;
        while ((status = inb(REG_STAT_CMD)) & (1 << STAT_BUSY));
        if (!(status & (1 << STAT_DRQ)) || (status & ((1 << STAT_DF) | (1 << STAT_ERR)))) {
            // 发生错误，直接return
            return -1;
        }
        // 一个扇区512字节，每次可以从数据寄存器读两个字节，所以一个扇区读256次
        for (int i = 0; i < 256; ++i) {
            *(reinterpret_cast<uint16_t*>(buffer) + i + 256 * s) = inw(REG_DATA);
        }
    }
    return 0;
}

int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void* buffer) {
    if (drive != DRIVE_MASTER) return -1; // 只有主设备，就偷懒了...
    // 等待设备空闲
    while((inb(REG_STAT_CMD) & (1 << STAT_BUSY)));
    // 选择设备
    outb(REG_DRIVE_SELECT, DRIVE_MASTER | ((lba >> 24) & 0xF));
    // 填入需要读取的扇区数
    outb(REG_SECTOR_COUNT, count);
    // 填入LBA
    outb(REG_LBA_LOW, (lba & 0xFF));
    outb(REG_LBA_MID, ((lba >> 8) & 0xFF));
    outb(REG_LBA_HIGH, ((lba >> 16) & 0xFF));
    // 发送写命令
    outb(REG_STAT_CMD, CMD_WRITE_SECTOR);
    uint16_t cnt = count == 0 ? 256 : count;
    for (int s = 0; s < cnt; ++s) {
        // 在扇区的边界执行检查即可
        uint8_t status;
        while ((status = inb(REG_STAT_CMD)) & (1 << STAT_BUSY));
        if (!(status & (1 << STAT_DRQ)) || (status & ((1 << STAT_DF) | (1 << STAT_ERR)))) {
            // 发生错误，直接return
            return -1;
        }
        // 一个扇区512字节，每次可以从数据寄存器读两个字节，所以一个扇区读256次
        for (int i = 0; i < 256; ++i) {
            outw(REG_DATA, *(reinterpret_cast<const uint16_t*>(buffer) + i + 256 * s));
        }
    }
    // 刷新缓存，保证写入的数据生效
    outb(REG_STAT_CMD, CMD_FLUSH_CACHE);
    while (inb(REG_STAT_CMD) & (1 << STAT_BUSY));
    return 0;
}

static int block_read(block_device* dev, uint32_t block_no, void* buffer) {
    return ata_read_sectors(dev->device_id, (block_no * dev->block_size) / dev->sector_size,
        dev->block_size / dev->sector_size, buffer);
}

static int block_write(block_device* dev, uint32_t block_no, const void* buffer) {
    return ata_write_sectors(dev->device_id, (block_no * dev->block_size) / dev->sector_size,
        dev->block_size / dev->sector_size, buffer);
}

void ata_init() {
    printf("ata initializing...");
    // 检测ATA设备，这里我们检测primary bus的master就够了
    outb(REG_DRIVE_SELECT, DRIVE_MASTER);
    outb(REG_SECTOR_COUNT, 0);
    outb(REG_LBA_LOW, 0);
    outb(REG_LBA_MID, 0);
    outb(REG_LBA_HIGH, 0);
    outb(REG_STAT_CMD, CMD_IDENTIFY);

    uint8_t master_stat = inb(REG_STAT_CMD);

    if (master_stat == 0) { // 没有设备
        return;
    }
    while((inb(REG_STAT_CMD) & (1 << STAT_BUSY)));


    uint8_t lba_mid = inb(REG_LBA_MID);
    uint8_t lba_high = inb(REG_LBA_HIGH);

    // 忽略其它类型的设备
    if (lba_mid != 0 || lba_high != 0) {
        return;
    }
    // 要把IDENTIFY的数据读走，否则设备状态会不正确
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
    bd->block_size = 0; // 这个我们只有在挂载后才知道
    bd->fs = file_system::UNKNOWN;
    bd->read = &block_read;
    bd->write = &block_write;

    int ret = register_block_device(bd);
    if (ret == -1) {
        printf("Failed to register master block device!\n");
    }
    printf("OK\n");
    return;
}
