## Homemade Operating System (20): RTL8139 NIC Driver (Part 1) — Driver Introduction, Initialization, and Send/Receive Logic Implementation

I don't normally develop drivers, so the closer something is to the hardware, the more I find a top-down approach appropriate.

The way to explain something should be: find the smallest concept you have a clear understanding of, then build from there. For me, that concept is the Ethernet frame.

### Target State

Since we're going top-down, let's start with the target state: our goal is to use `/dev/nic` as the virtual device file for our NIC driver. Reading from and writing to it essentially means reading and writing Ethernet frames. (We'll soon realize this is a **very bad idea** — but for now, it's not a bad way to debug.) Essentially, we need to implement two functions: `nic_read` and `nic_write`. `nic_write` sends an Ethernet frame, and `nic_read` returns an Ethernet frame.

Let's not overthink it — let's write a stub implementation first:

```cpp
#include <driver/devfs.hpp>
#include <driver/rtl8139.hpp>

void rtl8139_init() {
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
```

Then we can call these functions in `kernel_main` during initialization to initialize our driver:

```cpp
    extern void init_nic_dev_file(mounting_point* mp);
...
	mounting_point* dev_ret = v_mount(FS_DRIVER::DEVFS, "/dev", nullptr);
    if (dev_ret == nullptr) {
        panic("failed to mount devfs to /dev!");
    } else {
        printf("/dev mounted!\n");
    }
    init_console_dev(dev_ret);
    init_nic_dev_file(dev_ret);

...
    rtl8139_init();
```

This might feel like a victory lap — and it kind of is — but writing a driver isn't easy, so why not give yourself some feedback first?

Run the program and we can see the device file:

![image-20260304213711759](../assets/自制操作系统（20）：rtl8139网卡驱动（上）- 驱动介绍，初始化与收发数据逻辑实现/image-20260304213711759.png)

We can even write a test code to plant some seeds for the future:

```cpp
    PCB* cur_pcb = process_list[cur_process_id];
    int nic_fd = v_open(cur_pcb, "/dev/nic", O_RDONLY);
    if (nic_fd == -1) {
        printf("failed to open NIC dev!\n");
    }
    char* buf = (char*)kmalloc(1024 * sizeof(char));
    if ((v_read(cur_pcb, nic_fd, buf, 1024)) != -1) {
        printf("do nic_read successfully! %.8s\n", buf);
    } else {
        printf("failed to read NIC dev!\n");
    }
```

We believe that in the near future, this will properly return!

### Ring Buffer

So what does `nic_read` actually read? Well, after we configure the RTL8139, it will use DMA (Direct Memory Access) to write received data to memory based on the physical address we configure. So perhaps now, we can design where this buffer will be located in virtual address space and how large it should be.

The buffer size has several options: 8K/16K/32K/64K. We'll go with the maximum of 64KB. These sizes are determined by the `RBLEN` field in the RTL8139's `RCR` register, which we'll cover in detail when we configure the registers later.

```cpp
#include <kernel/mm.h>

constexpr uint32_t RBUFFER_SIZE = 0x10000;
constexpr uint32_t RBUFFER_ADDR_START = 0xE1000000;
constexpr uint32_t RBUFFER_ADDR_END = 0xE1000000 + 0x10000 + 0x10; // 64KB buffer + 16 bytes margin
static uint32_t p_addr = 0;

void round_buffer_init() {
    int total_pages = 0x10000 / (1 << 12) + 1; // one extra page for the 16-byte margin
    p_addr = (uint32_t)pmm_alloc(total_pages);
    for (int i = 0; i < total_pages; ++i) {
        vmm_map_page(p_addr + (1 << 12) * i, RBUFFER_ADDR_START + (1 << 12) * i,
        VMM_WRITABLE | VMM_CACHE_DISABLE); // Note: VMM_CACHE_DISABLE = no caching
    }
}

void rtl8139_init() {
    round_buffer_init();
    return;
}
```

We use `pmm_alloc` because we need a contiguous physical memory region — DMA writes directly to memory without going through the MMU.

Our current `vmm_map_page` implementation doesn't support setting the no-cache policy, so we need to modify it:

```cpp
    cur_pte->present       = 1;
    cur_pte->read_write    = (flag >> 1) & 1;
    cur_pte->user_super    = (flag >> 2) & 1;
    cur_pte->write_through = (flag >> 3) & 1;
    cur_pte->cache_disable = (flag >> 4) & 1;
    cur_pte->frame         = p_addr >> 12;
```

The 16-byte margin mentioned above is because the RTL8139 writes an extra 16 bytes at the end (for CRC, etc.), so we have to allocate an extra page to accommodate this.

Now, how do we tell the NIC that we've prepared a buffer for it? We need to "communicate" with it by reading and writing its registers.

### Port Communication — io_addr

When it comes to communication, we interact with this NIC hardware by writing data to its port address `io_addr`. Essentially, we're writing to its registers. Here's an example of configuring its buffer:

```cpp
constexpr uint16_t REG_CONFIG1  = 0x52;
constexpr uint16_t REG_CHIPCMD  = 0x37;
constexpr uint16_t REG_RBSTART  = 0x30;
constexpr uint16_t REG_RXCONFIG = 0x44;
constexpr uint16_t REG_TXCONFIG = 0x40;

static uint32_t p_addr = 0;

...

static uint16_t io_addr = 0;

void rtl8139_init() {
    round_buffer_init();

    outl(io_addr + REG_RBSTART, p_addr);
    return;
}
```

Note that I've conveniently defined the register offsets we'll need later as well.

Basically, `io_addr` is the base address for accessing this device. Once we have it, we can control the RTL8139 NIC. The offset (RBSTART) represents the register for the ring buffer start address. The register-to-offset mapping can be found in the NIC's datasheet.

Now we've told the RTL8139 where the buffer is. But how do we find out the NIC's `io_addr`? This brings us to how we discover the RTL8139.

### PCI Configuration Scanning

Many peripherals are connected via the PCI bus. Every device that supports the PCI protocol has a unique vendor ID and device ID. Each motherboard supports up to 256 buses (personal computers typically have one bus), each bus supports up to 32 devices, and each device supports up to 8 functions. From the OS perspective, this means up to 256 × 32 × 8 = 65,536 devices. By scanning all devices on the PCI bus, we can find the information for each device, like this:

```c++
int search_for_rtl8139() {
    uint8_t bus = 0;
    do {
        for (uint8_t dev = 0; dev < 32; ++dev)
            for (uint8_t func = 0; func < 8; ++func) {
                Get info...
                if (vendor ID and device ID match RTL8139) {
                    Get info and return
                }
            }
    } while (bus++ != 255);

    return -1;
}

void rtl8139_init() {
    if (round_buffer_init() == -1) return;

    outl(io_addr + REG_RBSTART, p_addr);
    return;
}
```

So how do we communicate with PCI to get specific information? Yes, you guessed it — still through our I/O ports! The function to get information looks like this:

```cpp
constexpr uint16_t PCI_CONFIG_ADDRESS = 0xCF8;
constexpr uint16_t PCI_CONFIG_DATA = 0xCFC;
uint32_t read_pci_by_32bits(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg_index) {
    uint32_t addr = (1u << 31) | (bus << 16) | (dev << 11) | (func << 8) | (reg_index << 2);
    // The highest bit is an enable bit, must be 1
    outl(PCI_CONFIG_ADDRESS, addr);
    // reg_index is left-shifted by 2 because we read 4 bytes at a time
    return inl(PCI_CONFIG_DATA);
}
```

The addresses above are the I/O port addresses for exchanging PCI configuration information.

Note the extra `reg_index` parameter in the function above — this is because PCI_CONFIG contains multiple fields:

![](../assets/自制操作系统（20）：rtl8139网卡驱动（上）- 驱动介绍，初始化与收发数据逻辑实现/Pci-config-space.svg.png)

By Vijay Kumar Vijaykumar - Own work, Public Domain, https://commons.wikimedia.org/w/index.php?curid=3181779

This is the PCI_CONFIG structure. We're interested in the Device ID and Vendor ID on the first row. `read_pci_by_32bits` returns 32 bits, so by passing `reg_index = 0`, we can read them:

```cpp
                // Get info...
                uint32_t config = read_pci_by_32bits(bus, dev, func, 0);
                uint16_t device_id = config >> 16;
                uint16_t vendor_id = config & ((1 << 16) - 1);
                if (vendor_id == 0x10ec && device_id == 0x8139) {
                    pci_bus = bus;
                    pci_dev = dev;
                    pci_func = func;
                    return 0;
                }
```

We can print the result to the console to confirm we found the right device:

```cpp
    if (search_for_rtl8139() == -1) return;
    printf("rtl8139 found! bus: %d, dev: %d, func: %d\n", pci_bus, pci_dev, pci_func);
```

![image-20260305001108055](../assets/自制操作系统（20）：rtl8139网卡驱动（上）- 驱动介绍，初始化与收发数据逻辑实现/image-20260305001108055.png)

Not found. That's because we haven't told QEMU to emulate an RTL8139 NIC yet. Add `-device rtl8139,netdev=net0 -netdev user,id=net0` to the boot command:

![image-20260305002040793](../assets/自制操作系统（20）：rtl8139网卡驱动（上）- 驱动介绍，初始化与收发数据逻辑实现/image-20260305002040793.png)

Don't forget we're looking for `io_addr`. It's in the BAR Address Registers at index 0 (the 4th entry in the table):

```cpp
    io_addr = read_pci_by_32bits(pci_bus, pci_dev, pci_func, 4) & ~0x3;
    printf("io_addr: %x\n", io_addr);
```

![image-20260305002611202](../assets/自制操作系统（20）：rtl8139网卡驱动（上）- 驱动介绍，初始化与收发数据逻辑实现/image-20260305002611202.png)

The bottom two bits of the value we read are flags, so we need to clear them.

### Initialization, and More Initialization

After getting `io_addr`, we need to do more device initialization. First, power on, then enable Bus Mastering (to start DMA), then perform a software reset, then set the buffer address (which we just did), and finally configure the receive/transmit settings.

#### Power On (Write 0x00 to Config1)

Our previously defined `REG_CONFIG1` comes into play:

```cpp
outb(io_addr + REG_CONFIG1, 0x00);
```

#### Enable Bus Mastering

Enabling Bus Mastering enables DMA; otherwise, we won't be able to read actual data.

It's said that most BIOSs enable it by default, but QEMU might not.

To enable it, set bit 2 (0-indexed) of the COMMAND register (the first half of the second row in PCI_CONFIG) to 1.

```cpp
void write_pci_by_32bits(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg_index, uint32_t data) {
    uint32_t addr = (1u << 31) | (bus << 16) | (dev << 11) | (func << 8) | (reg_index << 2);
    outl(PCI_CONFIG_ADDRESS, addr);
    outl(PCI_CONFIG_DATA, data);
}

    // enable bus mastering
    uint32_t dword = read_pci_by_32bits(pci_bus, pci_dev, pci_func, 1);
    uint16_t command = (dword & 0xFFFF);
    command |= (1 << 2); // bit 2 = Bus Master Enable
    write_pci_by_32bits(pci_bus, pci_dev, pci_func, 1, (dword & 0xFFFF0000) | command);
```

#### Software Reset

```cpp
    outb(io_addr + REG_CHIPCMD, 0x10);
    while (inb(io_addr + REG_CHIPCMD) & 0x10);
```

Setting the RTL8139's CHIPCMD to 0x10 triggers a software reset. Until the reset completes, bit 4 won't be cleared, so we need to wait in a loop.

#### Transmit/Receive Configuration

Transmit/Receive configuration sets the buffer size we mentioned earlier. The register bit annotations are as follows:

```cpp
    // Transmit/Receive configuration

    // define CR_RESET (1 << 4)
    // define CR_RECEIVER_ENABLE (1 << 3)
    // define CR_TRANSMITTER_ENABLE (1 << 2)
    // define CR_BUFFER_IS_EMPTY (1 << 0)
    outb(io_addr + REG_CHIPCMD, 0x0C); // Set RE, TE bits high, bits 2 and 3

    // define RCR_MXDMA_512 (5 << 8)
    // define RCR_MXDMA_1024 (6 << 8)
    // define RCR_MXDMA_UNLIMITED (7 << 8)
    // define RCR_ACCEPT_BROADCAST (1 << 3)
    // define RCR_ACCEPT_MULTICAST (1 << 2)
    // define RCR_ACCEPT_PHYS_MATCH (1 << 1)
    // RBLEN bits determine 64K, plus RCR_ACCEPT_BROADCAST, RCR_ACCEPT_PHYS_MATCH
    outl(io_addr + REG_RXCONFIG, 0x00001F0A);

    // define TCR_IFG_STANDARD (3 << 24)
    // define TCR_MXDMA_512 (5 << 8)
    // define TCR_MXDMA_1024 (6 << 8)
    // define TCR_MXDMA_2048 (7 << 8)
    outl(io_addr + REG_TXCONFIG, 0x03000700); // TCR_IFG_STANDARD, TCR_MXDMA_2048
```

### Sending Data

Now that initialization is complete, we can send data.

Sending data also requires specifying a contiguous physical address, which the RTL8139 reads via DMA. The maximum Ethernet frame size is 1536 bytes; we'll provide a bit of margin and use 2048 bytes. We send the physical address to the device via I/O ports, then send a command, and the device sends the data. Sending takes time, so to keep the CPU from waiting idly, the RTL8139 provides four send descriptors that we can use in rotation. This way, we can specify the next send immediately, providing higher throughput.

Like the ring buffer initialization, we initialize the send buffer at the start:

```cpp
constexpr uint32_t SEND_BUFFER_ADDR_START = 0xE1012000;
constexpr uint32_t SEND_BUFFER_SIZE_TOTAL = 0x2000;

constexpr uint8_t SEND_BUFFER_NUM = 4;
constexpr uint32_t SEND_BUFFER_SIZE = SEND_BUFFER_SIZE_TOTAL / SEND_BUFFER_NUM;

uint32_t send_buffer_paddr[4];
uint32_t send_buffer_vaddr[4];

void init_send_buffer() {
    uint8_t total_pages = SEND_BUFFER_SIZE_TOTAL / (1 << 12);
    uint32_t p_addr = (uint32_t)pmm_alloc(total_pages * 4096); // 4 * 2048

    for (int i = 0; i < SEND_BUFFER_NUM; ++i) {
        send_buffer_paddr[i] = p_addr + SEND_BUFFER_SIZE * i;
        send_buffer_vaddr[i] = SEND_BUFFER_ADDR_START + SEND_BUFFER_SIZE * i;
        if (i % (((1 << 12) / SEND_BUFFER_SIZE)) == 0) vmm_map_page(send_buffer_paddr[i], send_buffer_vaddr[i],
        VMM_WRITABLE | VMM_CACHE_DISABLE); // Map virtual addresses per page
    }
}
```

Then the send logic implementation:

```cpp
constexpr uint16_t REG_TSAD[4] = {0x20, 0x24, 0x28, 0x2C}; // Send buffer physical addresses
constexpr uint16_t REG_TSD[4]  = {0x10, 0x14, 0x18, 0x1C}; // Send status/control
static int tx_cur = 0;

static int nic_write(const char* buffer, uint32_t size) {
    if (size > SEND_BUFFER_SIZE) return -1;
    while(!(inl(io_addr + REG_TSD[tx_cur]) & (1 << 13))); // TSD bit 13 is the OWN bit — set to 1 when DMA completes

    memcpy((void*)send_buffer_vaddr[tx_cur], buffer, size);

    outl(io_addr + REG_TSAD[tx_cur], send_buffer_paddr[tx_cur]); // Physical address
    outl(io_addr + REG_TSD[tx_cur], size & 0x1FFF); // Bottom 13 bits = send length; clearing bit 13 triggers send

    tx_cur = (tx_cur + 1) % 4;
    return size;
}
```

Initially, bit 13 of the TSD register is 0, so the loop won't block. However, this loop needs a timeout mechanism — if the hardware malfunctions, our program will be blocked forever. We'll leave that for later.

Now that we've implemented the send logic, let's test it immediately:

```cpp
    char* buf = (char*)kmalloc(1024 * sizeof(char));
    if ((v_write(cur_pcb, nic_fd, buf, 1024)) != -1) {
        printf("/dev/nic do v_write successfully! %.8s\n", buf);
    } else {
        printf("failed to write to /dev/nic!\n");
    }
```

But what should we send? We need to construct an Ethernet frame, which consists of: destination MAC address, source MAC address, frame type, and payload.

- The destination MAC address can be all 1s (broadcast);
- The source MAC address can be a fake one;
- The frame type can be 0x88B5, indicating a locally experimental Ethernet frame;
- The payload can be anything. Let's fill in: "Hello world from LoliOS!"

```cpp
    char* buf = (char*)kmalloc(1024 * sizeof(char));
    memset(buf, 0xFF, 6); // Destination MAC, FF:FF:FF:FF:FF:FF = broadcast
    // Skip bytes 6-11
    buf[12] = 0x88; // 0x88B5 (IEEE reserved locally experimental type)
    buf[13] = 0xB5;
    memset(buf + 14, 0, 45);
    strcpy(buf + 14, "Hello world from LoliOS!");
    if ((v_write(cur_pcb, nic_fd, buf, 60)) != -1) { // Minimum Ethernet frame length is 60 bytes
        printf("/dev/nic do v_write successfully!\n");
    } else {
        printf("failed to write to /dev/nic!\n");
    }
```

![image-20260305111013390](../assets/自制操作系统（20）：rtl8139网卡驱动（上）- 驱动介绍，初始化与收发数据逻辑实现/image-20260305111013390.png)

It says it sent successfully, but it doesn't feel real. Let's use tcpdump to capture the packet.

#### tcpdump Packet Capture

```shell
qemu-system-i386 -cdrom lolios.iso \
    -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
    -device rtl8139,netdev=net0
```

Before capturing, we need to switch to tap mode. This lets our system use a virtual network cable to connect to a virtual NIC configured in WSL, like two hosts connected by a network cable linking their physical NICs. Some might ask: in the real world, things aren't usually connected this way — you'd normally connect to a router. But for our current purposes (including ARP later), this is sufficient.

We also need to create the tap device on the host first:

```shell
sudo ip tuntap add dev tap0 mode tap user $(whoami) # Create a tap device named tap0 for the current user in WSL
sudo ip link set tap0 up # Bring up the tap0 virtual NIC
```

Then start tcpdump:

```shell
sudo tcpdump -i tap0 -XX
```

Run the program and we can see the Ethernet frame we just sent:

```shell
aoverb@BA:~/lolios$ sudo tcpdump -i tap0 -XX
tcpdump: verbose output suppressed, use -v[v]... for full protocol decode
listening on tap0, link-type EN10MB (Ethernet), snapshot length 262144 bytes
11:36:17.556740 IP6 fe80::70a7:8ff:fe97:a0 > ff02::16: HBH ICMP6, multicast listener report v2, 1 group record(s), length 28
        0x0000:  3333 0000 0016 72a7 0897 00a0 86dd 6000  33....r.......`.
        0x0010:  0000 0024 0001 fe80 0000 0000 0000 70a7  ...$..........p.
        0x0020:  08ff fe97 00a0 ff02 0000 0000 0000 0000  ................
        0x0030:  0000 0000 0016 3a00 0502 0000 0100 8f00  ......:.........
        0x0040:  f6f3 0000 0001 0400 0000 ff02 0000 0000  ................
        0x0050:  0000 0000 0001 ff97 00a0                 ..........
11:36:18.240607 IP6 fe80::70a7:8ff:fe97:a0 > ff02::16: HBH ICMP6, multicast listener report v2, 1 group record(s), length 28
        0x0000:  3333 0000 0016 72a7 0897 00a0 86dd 6000  33....r.......`.
        0x0010:  0000 0024 0001 fe80 0000 0000 0000 70a7  ...$..........p.
        0x0020:  08ff fe97 00a0 ff02 0000 0000 0000 0000  ................
        0x0030:  0000 0000 0016 3a00 0502 0000 0100 8f00  ......:.........
        0x0040:  f6f3 0000 0001 0400 0000 ff02 0000 0000  ................
        0x0050:  0000 0000 0001 ff97 00a0                 ..........
11:36:18.906045 00:00:00:00:00:00 (oui Ethernet) > Broadcast, ethertype Unknown (0x88b5), length 60:
        0x0000:  ffff ffff ffff 0000 0000 0000 88b5 4865  ..............He
        0x0010:  6c6c 6f20 776f 726c 6420 6672 6f6d 204c  llo.world.from.L <- Here!
        0x0020:  6f6c 694f 5321 0000 0000 0000 0000 0000  oliOS!..........
        0x0030:  0000 0000 0000 0000 0000 0000            ............
```

Feels great! Now let's look at how to receive data.

### Receiving Data

Receiving data requires reading a status register to confirm that new data has been written to the ring buffer.

We also need to record an offset (initialized to 0) to track the last read position.

After confirming new data, we read the buffer and extract status and length information from its header. The status tells us whether the current packet is valid.

Regardless of status, we advance the pointer based on the length information (while also recording the old pointer).

If the packet is valid, we copy the content based on the length to the prepared receive buffer.

At the same time, we update a register to tell the NIC how far we've read (used by the NIC to determine overflow):

```cpp
constexpr uint16_t REG_CAPR = 0x38;

static int rx_cur = 0;

static int nic_read(char* buffer, uint32_t /* offset */, uint32_t size) {
    // Check if new data has been written to the ring buffer
    if (inb(io_addr + REG_CHIPCMD) & (1 << 0x0)) return -1;   // BUFE bit, bit 0, buffer empty flag

    uint32_t* header = reinterpret_cast<uint32_t*>(RBUFFER_ADDR_START + rx_cur);
    uint16_t length = (*header) >> 16; // Length includes the trailing 4-byte CRC
    uint16_t status = (*header) & 0xFFFF;

    int old_rx_cur = rx_cur;
    rx_cur += length + 4; // Include the 4-byte header length
    rx_cur = (rx_cur + 3) & (~3); // Align to 4 bytes
    rx_cur %= RBUFFER_SIZE; // Normal movement of the current pointer

    if (!(status & 0x01)) return -1; // ROK bit, bit 0

    int data_cur = (old_rx_cur + 4) % RBUFFER_SIZE; // Skip the 4-byte header, start reading from old_rx_cur + 4
    int data_end_cur = (old_rx_cur + length - 4) % RBUFFER_SIZE;  // Stop 4 bytes before the end

    int readcnt = 0;

    for (int i = 0; i < size; ++i) {
        if (((data_cur + i) % RBUFFER_SIZE) == data_end_cur) break;
        buffer[i] = ((char*)(RBUFFER_ADDR_START))[(data_cur + i) % RBUFFER_SIZE];
        ++readcnt;
    }

    // Tell the NIC our current read address. Subtracting 16 bytes is a hardware requirement.
    // Pay attention to register width, especially when writing...
    outw(io_addr + REG_CAPR, (rx_cur - 0x10 + RBUFFER_SIZE) % RBUFFER_SIZE);
    
    return readcnt;
}
```

Now we can use our implemented driver to send and receive Ethernet frames!

---

Next, we'll use this driver to do something cooler: send an Ethernet request to ask for the MAC address of our host (WSL), so we'll receive an Ethernet frame containing the host's MAC address reply.

But before doing that, we still need to know our own MAC address. Due to length constraints, we'll stop here for today and continue in the next section.
