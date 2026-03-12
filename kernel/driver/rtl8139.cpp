#include <driver/devfs.hpp>
#include <driver/rtl8139.hpp>
#include <driver/pit.h>
#include <kernel/hal.h>
#include <kernel/isr.h>
#include <kernel/io.h>
#include <kernel/net/net.hpp>
#include <kernel/mm.hpp>
#include <kernel/spinlock.hpp>
#include <kernel/process.hpp>
#include <kernel/schedule.h>

#include <stdio.h>
#include <string.h>

constexpr uint16_t REG_IMR = 0x3C;
constexpr uint16_t REG_ISR = 0x3E;
constexpr uint16_t REG_CONFIG1  = 0x52;
constexpr uint16_t REG_CHIPCMD  = 0x37;
constexpr uint16_t REG_RBSTART  = 0x30;
constexpr uint16_t REG_RXCONFIG = 0x44;
constexpr uint16_t REG_TXCONFIG = 0x40;
constexpr uint16_t REG_CAPR = 0x38;

spinlock rbuffer_lock;
constexpr uint32_t RBUFFER_SIZE = 0x10000;
constexpr uint32_t RBUFFER_ADDR_START = 0xE1000000;
constexpr uint32_t RBUFFER_ADDR_END = 0xE1000000 + 0x10000 + 0x10; // 64kb缓冲区映射 + 16字节的余量
constexpr uint32_t SEND_BUFFER_ADDR_START = 0xE1012000;
constexpr uint32_t SEND_BUFFER_SIZE_TOTAL = 0x2000;

static uint32_t is_initialized = 0;
static uint32_t p_addr = 0;

struct net_packet {
    char data[1800];
    uint16_t len;
};

constexpr uint32_t NET_QUEUE_SIZE = 64;
static net_packet net_pkt_queue[NET_QUEUE_SIZE];
static volatile uint32_t nq_head = 0;  // 中断写
static volatile uint32_t nq_tail = 0;  // 内核线程读
static pid_t net_thread_pid = 0;

void round_buffer_init() {
    int total_pages = RBUFFER_SIZE / (1 << 12) + 1; // 多出的一页是为了16字节的余量
    p_addr = (uint32_t)pmm_alloc(RBUFFER_SIZE);
    for (int i = 0; i < total_pages; ++i) {
        vmm_map_page(p_addr + (1 << 12) * i, RBUFFER_ADDR_START + (1 << 12) * i,
        VMM_WRITABLE | VMM_CACHE_DISABLE);
    }
}
static uint8_t irq = 0; // 网卡的IRQ号
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

constexpr uint8_t SEND_BUFFER_NUM = 4;
constexpr uint32_t SEND_BUFFER_SIZE = SEND_BUFFER_SIZE_TOTAL / SEND_BUFFER_NUM;

uint32_t send_buffer_paddr[4];
uint32_t send_buffer_vaddr[4];
spinlock send_buffer_lock;

void init_send_buffer() {
    uint8_t total_pages = SEND_BUFFER_SIZE_TOTAL / (1 << 12);
    uint32_t p_addr = (uint32_t)pmm_alloc(total_pages * 4096); // 4 * 2048

    for (int i = 0; i < SEND_BUFFER_NUM; ++i) {
        send_buffer_paddr[i] = p_addr + SEND_BUFFER_SIZE * i;
        send_buffer_vaddr[i] = SEND_BUFFER_ADDR_START + SEND_BUFFER_SIZE * i;
        if (i % (((1 << 12) / SEND_BUFFER_SIZE)) == 0) vmm_map_page(send_buffer_paddr[i], send_buffer_vaddr[i],
        VMM_WRITABLE | VMM_CACHE_DISABLE); // 以每页为单位映射虚拟地址
    }
}

static int rx_cur = 0;

static int nic_read(char* buffer, uint32_t /* offset */, uint32_t size) {
    // 确认是否有新数据写入到环形缓冲区中
    if (inb(io_addr + REG_CHIPCMD) & (1 << 0x0)) return -1;   // BUFE 位，bit 0，缓冲区空标记
    SpinlockGuard guard(rbuffer_lock);
    uint32_t* header = reinterpret_cast<uint32_t*>(RBUFFER_ADDR_START + rx_cur);
    uint16_t length = (*header) >> 16; // 这里的长度包含尾部4字节的CRC
    uint16_t status = (*header) & 0xFFFF;

    int old_rx_cur = rx_cur;
    rx_cur += length + 4; // 把头部4字节长度算上
    rx_cur = (rx_cur + 3) & (~3); // 四字节对齐
    rx_cur %= RBUFFER_SIZE; // 当前指针的位置正常移动

    if (!(status & 0x01)) return -1; // ROK 位，bit 0

    int data_cur = (old_rx_cur + 4) % RBUFFER_SIZE; // 忽略前4字节的头部，直接从old_rx_cur + 4开始读起
    int data_end_cur = (old_rx_cur + length) % RBUFFER_SIZE;  // 读到尾部倒数第四个字节不读了，后面四个字节是CRC

    int readcnt = 0;

    for (int i = 0; i < size; ++i) {
        if (((data_cur + i) % RBUFFER_SIZE) == data_end_cur) break;
        buffer[i] = ((char*)(RBUFFER_ADDR_START))[(data_cur + i) % RBUFFER_SIZE];
        ++readcnt;
    }

    // 告诉网卡我们当前的读取地址，减去16字节是硬件要求
    // 要注意寄存器的长度，尤其是写的时候...
    outw(io_addr + REG_CAPR, (rx_cur - 0x10 + RBUFFER_SIZE) % RBUFFER_SIZE);
    
    return readcnt;
}

static char recv_buff[1800];
void ethernet_handler(char* buffer, uint16_t size);

static void net_enqueue(const char* data, uint16_t len) {
    uint32_t next_head = (nq_head + 1) % NET_QUEUE_SIZE;
    if (next_head == nq_tail) return;
    memcpy(net_pkt_queue[nq_head].data, data, len);
    net_pkt_queue[nq_head].len = len;
    nq_head = next_head;
}

static void net_thread_wakeup() {
    if (net_thread_pid == 0) return;
    uint32_t flags = spinlock_acquire(&process_list_lock);
    if (process_list[net_thread_pid] &&
        process_list[net_thread_pid]->state == process_state::SLEEPING) {
        process_list[net_thread_pid]->state = process_state::READY;
        insert_into_scheduling_queue(net_thread_pid);
    }
    spinlock_release(&process_list_lock, flags);
}

void rtl8139_interrupt_handler(registers*) {
    uint16_t status = inw(io_addr + REG_ISR);
    if (status & 0x0001) { // 收到了包
        while (!(inb(io_addr + REG_CHIPCMD) & 0x01)) {  // 缓冲区不为空
            int len = nic_read(recv_buff, 0, sizeof(recv_buff));
            if (len > 0)
                net_enqueue(recv_buff, len);
        }
        net_thread_wakeup();
    }

    outw(io_addr + REG_ISR, status);  // 写回清除
    if (irq + 0x20 >= 0x28) {
        outb(0xA0, 0x20);  // slave EOI
    }
    outb(0x20, 0x20);      // master EOI
    return;
}

void reg_isr() {
    // 读取网卡的中断号
    irq = read_pci_by_32bits(pci_bus, pci_dev, pci_func, 15) & 0xFF;
    register_interrupt_handler(irq + 0x20, rtl8139_interrupt_handler); // 加上从片偏移 减去在主片的7个设备

    hal_enable_irq(2);   // 打开从片
    hal_enable_irq(irq); // 打开IRQ 11 本身

    // 开启接收OK和发送OK的中断
    outw(io_addr + REG_IMR, 0x0001 | 0x0004);  // ROK | TOK
}

void net_rx_thread_main(void*) {
    while (1) {
        while (nq_tail != nq_head) {
            net_packet* pkt = &net_pkt_queue[nq_tail];
            ethernet_handler(pkt->data, pkt->len);
            nq_tail = (nq_tail + 1) % NET_QUEUE_SIZE;
        }

        uint32_t flags = spinlock_acquire(&process_list_lock);
        if (nq_tail == nq_head) {
            process_list[cur_process_id]->state = process_state::SLEEPING;
            spinlock_release(&process_list_lock, flags);
            yield();
        } else {
            spinlock_release(&process_list_lock, flags);
        }
    }
}

void rtl8139_init() {
    if (search_for_rtl8139() == -1) return;
    printf("rtl8139 found! bus: %d, dev: %d, func: %d...", pci_bus, pci_dev, pci_func);
    io_addr = read_pci_by_32bits(pci_bus, pci_dev, pci_func, 4) & ~0x3;
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

    init_send_buffer();
    reg_isr();
    is_initialized = 1;
    net_thread_pid = create_process(KERNEL_PROC_NAME_KNET, (void*)&net_rx_thread_main, nullptr);
    printf("OK\n");
    return;
}

constexpr uint16_t REG_TSAD[4] = {0x20, 0x24, 0x28, 0x2C}; // 发送缓冲区物理地址
constexpr uint16_t REG_TSD[4]  = {0x10, 0x14, 0x18, 0x1C}; // 发送状态/控制
static int tx_cur = 0;

int nic_write(const char* buffer, uint32_t size) {
    if (size > SEND_BUFFER_SIZE || !is_initialized) return -1;

    SpinlockGuard guard(send_buffer_lock);

    uint32_t old_ticks = pit_get_ticks();
    while(!(inl(io_addr + REG_TSD[tx_cur]) & (1 << 13))) {
        if (pit_get_ticks() - old_ticks > 100) { // 1秒的时间上限
            return -1;
        }
    }

    memcpy((void*)send_buffer_vaddr[tx_cur], buffer, size);

    outl(io_addr + REG_TSAD[tx_cur], send_buffer_paddr[tx_cur]); // 物理地址
    outl(io_addr + REG_TSD[tx_cur], size & 0x1FFF); // 前13位代表发送长度，第13位置零代表发送

    tx_cur = (tx_cur + 1) % 4;
    return size;
}

void get_mac(uint8_t mac[6]) {
    if (!is_initialized) return;
    for (int i = 0; i < 6; ++i)
        mac[i] = inb(io_addr + i);
}

static int nic_mac_read(char* buffer, uint32_t, uint32_t size) {
    uint32_t len = size < 6 ? size : 6;
    memcpy(buffer, &(getLocalNetconf()->mac), len);
    return len;
}

static int nic_mac_write(const char*, uint32_t) { return -1; } // 不支持写MAC

void init_nic_dev_file(mounting_point* mp) {
    static dev_operation nic_mac_opr;
    nic_mac_opr.read = &nic_mac_read;
    nic_mac_opr.write = &nic_mac_write;
    register_in_devfs(mp, "nic_mac", &nic_mac_opr);
}

