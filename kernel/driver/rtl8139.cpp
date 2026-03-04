#include <driver/devfs.hpp>
#include <driver/rtl8139.hpp>

#include <kernel/io.h>
#include <kernel/mm.hpp>

#include <stdio.h>

constexpr uint16_t REG_CONFIG1  = 0x52;
constexpr uint16_t REG_CHIPCMD  = 0x37;
constexpr uint16_t REG_RBSTART  = 0x30;
constexpr uint16_t REG_RXCONFIG = 0x44;
constexpr uint16_t REG_TXCONFIG = 0x40;

constexpr uint32_t RBUFFER_SIZE = 0x10000;
constexpr uint32_t RBUFFER_ADDR_START = 0xE1000000;
constexpr uint32_t RBUFFER_ADDR_END = 0xE1000000 + 0x10000 + 0x10; // 64kb缓冲区映射 + 16字节的余量
static uint32_t p_addr = 0;

void round_buffer_init() {
    int total_pages = 0x10000 / (1 << 12) + 1; // 多出的一页是为了16字节的余量
    p_addr = (uint32_t)pmm_alloc(total_pages);
    for (int i = 0; i < total_pages; ++i) {
        vmm_map_page(p_addr + (1 << 12) * i, RBUFFER_ADDR_START + (1 << 12) * i,
        VMM_WRITABLE | VMM_CACHE_DISABLE);
    }
}

static uint16_t io_addr = 0;
static uint8_t pci_bus = 0;
static uint8_t pci_dev = 0;
static uint8_t pci_func = 0;

constexpr uint16_t PCI_CONFIG_ADDRESS = 0xCF8;
constexpr uint16_t PCI_CONFIG_DATA = 0xCFC;
uint32_t read_pci_by_32bits(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg_index) {
    uint32_t addr = (1u << 31) | (bus << 16) | (dev << 11) | (func << 8) | (reg_index << 2);
    outl(PCI_CONFIG_ADDRESS, addr);
    // reg_index左移两位是因为我们每次读取4个字节
    return inl(PCI_CONFIG_DATA);
}

void write_pci_by_32bits(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg_index, uint32_t data) {
    uint32_t addr = (1u << 31) | (bus << 16) | (dev << 11) | (func << 8) | (reg_index << 2);
    outl(PCI_CONFIG_ADDRESS, addr);
    outl(PCI_CONFIG_DATA, data);
}

int search_for_rtl8139() {
    uint8_t bus = 0;
    do {
        for (uint8_t dev = 0; dev < 32; ++dev)
            for (uint8_t func = 0; func < 8; ++func) {
                // 获取信息...
                uint32_t config = read_pci_by_32bits(bus, dev, func, 0);
                uint16_t device_id = config >> 16;
                uint16_t vendor_id = config & ((1 << 16) - 1);
                if (vendor_id == 0x10ec && device_id == 0x8139) {
                    pci_bus = bus;
                    pci_dev = dev;
                    pci_func = func;
                    return 0;
                }
            }
    } while (bus++ != 255);

    return -1;
}

void rtl8139_init() {
    if (search_for_rtl8139() == -1) return;
    printf("rtl8139 found! bus: %d, dev: %d, func: %d\n", pci_bus, pci_dev, pci_func);
    io_addr = read_pci_by_32bits(pci_bus, pci_dev, pci_func, 4) & ~0x3;
    printf("io_addr: %x\n", io_addr);
    outb(io_addr + REG_CONFIG1, 0x00);

    // enable bus mastering
    uint32_t dword = read_pci_by_32bits(pci_bus, pci_dev, pci_func, 1);
    uint16_t command = (dword & 0xFFFF);
    command |= (1 << 2); // bit 2 = Bus Master Enable
    write_pci_by_32bits(pci_bus, pci_dev, pci_func, 1, (dword & 0xFFFF0000) | command);

    // software reset
    outb(io_addr + REG_CHIPCMD, 0x10);
    while (inb(io_addr + REG_CHIPCMD) & 0x10);

    round_buffer_init();

    // round buffer addr setup
    outl(io_addr + REG_RBSTART, p_addr);

    // 收发配置

    // define CR_RESET (1 << 4)
    // define CR_RECEIVER_ENABLE (1 << 3)
    // define CR_TRANSMITTER_ENABLE (1 << 2)
    // define CR_BUFFER_IS_EMPTY (1 << 0)
    outb(io_addr + REG_CHIPCMD, 0x0C); // 设置RE, TE位为高，第二第三位

    // define RCR_MXDMA_512 (5 << 8)
    // define RCR_MXDMA_1024 (6 << 8)
    // define RCR_MXDMA_UNLIMITED (7 << 8)
    // define RCR_ACCEPT_BROADCAST (1 << 3)
    // define RCR_ACCEPT_MULTICAST (1 << 2)
    // define RCR_ACCEPT_PHYS_MATCH (1 << 1)
    // 这里 RBLEN 位决定了 64K, 还有 RCR_ACCEPT_BROADCAST，RCR_ACCEPT_PHYS_MATCH
    outl(io_addr + REG_RXCONFIG, 0x00001F0A);

    // define TCR_IFG_STANDARD (3 << 24)
    // define TCR_MXDMA_512 (5 << 8)
    // define TCR_MXDMA_1024 (6 << 8)
    // define TCR_MXDMA_2048 (7 << 8)
    outl(io_addr + REG_TXCONFIG, 0x03000700); // TCR_IFG_STANDARD, TCR_MXDMA_2048

    return;
}

static int nic_read(char* buffer, uint32_t offset, uint32_t size) {
    return -1;
}

static int nic_write(const char* buffer, uint32_t size) {
    return -1;
}

void init_nic_dev_file(mounting_point* mp) {
    static dev_operation nic_opr;
    nic_opr.read = &nic_read;
    nic_opr.write = &nic_write;
    register_in_devfs(mp, "nic", &nic_opr);
};
