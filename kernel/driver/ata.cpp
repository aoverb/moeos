#include <kernel/io.h>
#include <kernel/mm.hpp>
#include <driver/ata.hpp>
#include <driver/block.hpp>
#include <string.h>

constexpr uint16_t REG_DATA = 0x1F0;
constexpr uint16_t REG_SECTOR_COUNT = 0x1F2;
constexpr uint16_t REG_LBA_LOW = 0x1F3;
constexpr uint16_t REG_LBA_MID = 0x1F4;
constexpr uint16_t REG_LBA_HIGH = 0x1F5;
constexpr uint16_t REG_DRIVE_SELECT = 0x1F6;
constexpr uint16_t REG_STAT_CMD = 0x1F7;
constexpr uint16_t REG_ALT_STAT = 0x3F6; // 备用状态寄存器，用于延迟

constexpr uint8_t CMD_READ_SECTOR = 0x20;
constexpr uint8_t CMD_WRITE_SECTOR = 0x30;
constexpr uint8_t CMD_IDENTIFY = 0xEC;
constexpr uint8_t CMD_FLUSH_CACHE = 0xE7;

constexpr uint8_t DRIVE_MASTER = 0xE0; // master选择 + LBA模式

constexpr uint8_t STAT_ERR = 0;
constexpr uint8_t STAT_DRQ = 3; // Data Request
constexpr uint8_t STAT_DF = 5;  // Drive Fault
constexpr uint8_t STAT_BUSY = 7;

// 忙等待的超时阈值（循环次数）
// 真机上ATA操作通常不会超过几微秒到几毫秒，这里给一个宽松的阈值
constexpr uint32_t ATA_TIMEOUT = 10000000;

/**
 * 等待 BSY 位清零，带超时
 * 返回 true 表示等待成功（BSY已清除），false 表示超时
 */
static bool ata_wait_busy() {
    uint32_t timeout = ATA_TIMEOUT;
    while ((inb(REG_STAT_CMD) & (1 << STAT_BUSY))) {
        if (--timeout == 0) {
            return false; // 超时
        }
    }
    return true;
}

/**
 * 等待设备准备好数据（BSY 清除且 DRQ 置位），带超时
 * 返回 0 表示成功，-1 表示超时或错误
 */
static int ata_wait_drq() {
    uint32_t timeout = ATA_TIMEOUT;
    uint8_t status;
    do {
        status = inb(REG_STAT_CMD);
        if (--timeout == 0) {
            return -1; // 超时
        }
    } while ((status & (1 << STAT_BUSY)));

    if (!(status & (1 << STAT_DRQ)) || (status & ((1 << STAT_DF) | (1 << STAT_ERR)))) {
        return -1; // 错误
    }
    return 0;
}

/**
 * 执行 400ns 延迟（通过读取备用状态寄存器）
 * ATA 规范要求在操作之间等待至少 400ns
 */
static void ata_delay_400ns() {
    // 读取备用状态寄存器 4 次，每次约 100ns (在典型硬件上)
    inb(REG_ALT_STAT);
    inb(REG_ALT_STAT);
    inb(REG_ALT_STAT);
    inb(REG_ALT_STAT);
}

int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buffer) {
    if (drive != DRIVE_MASTER) return -1; // 只有主设备，就偷懒了...
    // 等待设备空闲（带超时）
    if (!ata_wait_busy()) return -1;
    // 选择设备
    outb(REG_DRIVE_SELECT, DRIVE_MASTER | ((lba >> 24) & 0xF));
    ata_delay_400ns(); // 真机需要等待设备稳定
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
        // 等待设备准备好数据（带超时）
        if (ata_wait_drq() != 0) {
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
    // 等待设备空闲（带超时）
    if (!ata_wait_busy()) return -1;
    // 选择设备
    outb(REG_DRIVE_SELECT, DRIVE_MASTER | ((lba >> 24) & 0xF));
    ata_delay_400ns(); // 真机需要等待设备稳定
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
        // 等待设备准备好接收数据（带超时）
        if (ata_wait_drq() != 0) {
            return -1;
        }
        // 一个扇区512字节，每次可以从数据寄存器读两个字节，所以一个扇区读256次
        for (int i = 0; i < 256; ++i) {
            outw(REG_DATA, *(reinterpret_cast<const uint16_t*>(buffer) + i + 256 * s));
        }
    }
    // 刷新缓存，保证写入的数据生效
    outb(REG_STAT_CMD, CMD_FLUSH_CACHE);
    ata_wait_busy(); // 带超时的等待
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

extern "C" int printf(const char* fmt, ...);
void ata_init() {
    printf("ata initializing...");

    // ----- 第一步：软复位 primary bus -----
    // 向控制寄存器写 0x04（SRST）来复位总线
    outb(0x3F6, 0x04);  // 复位
    ata_delay_400ns();
    outb(0x3F6, 0x00);  // 取消复位
    ata_delay_400ns();
    // 等待设备就绪（带超时）
    if (!ata_wait_busy()) {
        printf("no response after reset, skip\n");
        return;
    }

    // ----- 第二步：检测设备是否存在 -----
    // 选择 master 设备
    outb(REG_DRIVE_SELECT, DRIVE_MASTER);
    ata_delay_400ns();

    // 读取状态寄存器
    uint8_t master_stat = inb(REG_STAT_CMD);

    // 真机上不存在的设备返回 0xFF（浮空总线），QEMU 返回 0
    // 两种都需要检测
    if (master_stat == 0 || master_stat == 0xFF) {
        printf("no device\n");
        return;
    }

    // 发送 IDENTIFY 命令
    outb(REG_SECTOR_COUNT, 0);
    outb(REG_LBA_LOW, 0);
    outb(REG_LBA_MID, 0);
    outb(REG_LBA_HIGH, 0);
    outb(REG_STAT_CMD, CMD_IDENTIFY);

    // 再次读取状态，确认设备有响应
    master_stat = inb(REG_STAT_CMD);
    if (master_stat == 0 || master_stat == 0xFF) {
        printf("no response to IDENTIFY\n");
        return;
    }

    // 等待设备就绪（带超时）
    if (!ata_wait_busy()) {
        printf("IDENTIFY timeout\n");
        return;
    }

    uint8_t lba_mid = inb(REG_LBA_MID);
    uint8_t lba_high = inb(REG_LBA_HIGH);

    // 忽略其它类型的设备（ATAPI 设备会返回非零值）
    if (lba_mid != 0 || lba_high != 0) {
        printf("non-ATA device\n");
        return;
    }

    // 要把IDENTIFY的数据读走，否则设备状态会不正确
    uint16_t identify[256];
    for (int i = 0; i < 256; ++i) {
        identify[i] = inw(REG_DATA);
    }

    uint32_t total_sectors = (uint32_t)identify[61] << 16 | identify[60];
    if (total_sectors == 0) {
        printf("zero sectors, skip\n");
        return;
    }

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
    printf("OK (%d sectors)\n", static_cast<int32_t>(total_sectors));
    return;
}
