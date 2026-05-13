## Homemade Operating System (21): RTL8139 NIC Driver (Part 2) — Interrupt Handling and ARP Support

Let's continue writing the driver and implement a function to get the MAC address.

### Getting the MAC Address

Getting the MAC address also involves reading a certain register of the NIC driver. The RTL8139's MAC address is stored at I/O port offsets `0x00 ~ 0x05`, so we can read it byte by byte:

```cpp
static void get_mac(uint8_t mac[6]) {
    for (int i = 0; i < 6; ++i)
        mac[i] = inb(io_addr + i);
}
```

I also wrapped this NIC MAC up as a device file:

```cpp
static int nic_mac_read(char* buffer, uint32_t /* offset */, uint32_t size) {
    uint8_t mac[6];
    get_mac(mac);
    for (int i = 0; i < size; ++i) {
        if (i >= 6) return 6;
        buffer[i] = mac[i];
    }
    return size;
}

static int nic_mac_write(const char*, uint32_t) { return -1; } // Doesn't support writing MAC

void init_nic_dev_file(mounting_point* mp) {
    static dev_operation nic_opr;
    nic_opr.read = &nic_read;
    nic_opr.write = &nic_write;
    register_in_devfs(mp, "nic", &nic_opr);

    static dev_operation nic_mac_opr;
    nic_mac_opr.read = &nic_mac_read;
    nic_mac_opr.write = &nic_mac_write;
    register_in_devfs(mp, "nic_mac", &nic_mac_opr);
};
```

Let's go back to where we send Ethernet frames, read our device file to get the MAC, and fill it into the Ethernet frame's source MAC field:

```cpp
...
    int nic_mac_fd = v_open(cur_pcb, "/dev/nic_mac", O_RDONLY);
    if (nic_mac_fd == -1) {
        printf("failed to open NIC dev!\n");
    }

    uint8_t mac[6];
    if (v_read(cur_pcb, nic_mac_fd, reinterpret_cast<char*>(mac), 6)) {
        printf("mac: %X:%X:%X:%X:%X:%X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    char* buf = (char*)kmalloc(1024 * sizeof(char));
    memset(buf, 0xFF, 6); // Destination MAC, FF:FF:FF:FF:FF:FF = broadcast
    memcpy(buf + 6, mac, 6);
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

Then, when starting QEMU, we configure a MAC address for our NIC:

```shell
qemu-system-i386 -cdrom lolios.iso \
    -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
    -device rtl8139,netdev=net0,mac=CA:FE:BA:BE:13:37
```

Boot up, and we can see the configured MAC address output on the console:

![image-20260305173858250](../assets/自制操作系统（21）：rtl8139网卡驱动（下）- 中断处理与ARP支持/image-20260305173858250.png)

tcpdump can also see it.

![image-20260305173821064](../assets/自制操作系统（21）：rtl8139网卡驱动（下）- 中断处理与ARP支持/image-20260305173821064.png)

### "A Terrible Idea"

Remember how I said in the previous chapter that treating the NIC as a file was a terrible idea? At that time, I was still basking in the afterglow of having completed a simple file system and couldn't shift my thinking. So when I stepped into the network stack, I didn't switch from the file reading mindset.

Our write operation is fine. I had a vague feeling that "reading the NIC's receive buffer" was a bad idea, but what exactly was wrong? The first thing I thought of was safety: firstly, we can see any frame; secondly, our read operation has a cost — it prevents whoever originally wanted to read that data from reading it (because the buffer pointer has already moved past it!). This was before I looked at what ARP is (ARP will be introduced below — you can think of it as a specific component that uses the Ethernet frame reading feature).

When I looked at ARP and wanted to implement it, I found I first needed to write logic to read and parse Ethernet frames. So I would implement it in the Ethernet code. But at this point, I ran into a specific dilemma: first, I don't know when the NIC has data to read and when it doesn't — this means I can only use a `while` loop to poll; second, I can only know if something is what I want after reading it, so I have to keep waiting. Worse still, other processes suffer too — I've consumed their data! If they're also polling, they'll never find what they're waiting for! Gradually, it dawned on me: I had unconsciously equated reading network data with reading files.

Files are static and deterministic. Files don't change. It's like a convenience store downstairs — it's always there, so "pulling" data from it is fine. But now I'm like waiting on a busy street for a speeding delivery truck that has my package. Every time I see a delivery truck, I stop it and immediately sign for the package on it. If it's not what I want, I keep waiting. Not only is this troublesome, but I've also thrown away someone else's package, making it so they can never receive their delivery!

So we need to change our approach. Facing the ever-changing network "packages," what we should do is: design it so that when a package arrives, the sorting station delivers it directly to us for signing. That way there's no problem.

The above and below represent the difference between the **pull model** and the **push model**. In fact, every layer of the network stack works this way — registration and push. The NIC can set up interrupt handlers, and we can write logic inside them to push the packet to the Ethernet frame handler. The Ethernet frame handler parses it, sees which protocol the frame belongs to, and pushes it to the specified protocol handler...

This is a hole I fell into, and I'm sharing it with you. Once you understand the above points, the remediation is actually quite simple. Let's see how to assign an interrupt handler to the RTL8139.

### Interrupt Handler

![](../assets/自制操作系统（21）：rtl8139网卡驱动（下）- 中断处理与ARP支持/Pci-config-space.svg_1.png)

By Vijay Kumar Vijaykumar - Own work, Public Domain, https://commons.wikimedia.org/w/index.php?curid=3181779

Let's look back at this diagram. We need to read the interrupt line, which contains the IRQ number. We use it together with our interrupt handler registration function to register the interrupt:

```cpp
void rtl8139_interrupt_handler(registers*) {
    // Read and push
}

void reg_isr() {
    uint8_t irq = read_pci_by_32bits(pci_bus, pci_dev, pci_func, 15) & 0xFF;
    register_interrupt_handler(irq, rtl8139_interrupt_handler);
}
```

Registration is that simple. Don't forget to call `reg_isr()` at the end of the driver's initialization function.

```cpp
static char recv_buff[1800];
void ethernet_handler(char* buffer, uint16_t size);

void rtl8139_interrupt_handler(registers*) {
    uint16_t status = inw(io_addr + REG_ISR);

    if (status & 0x0001) { // Packet received
        while (!(inb(io_addr + REG_CHIPCMD) & 0x01)) {  // Buffer not empty
            int len = nic_read(recv_buff, 0, sizeof(recv_buff));
            if (len > 0)
                ethernet_handler(recv_buff, len);
        }
    }

    outw(io_addr + REG_ISR, status);  // Write back to clear
    return;
}
```

If two interrupts occur close together, one might be lost, so we use a loop to read, ensuring the buffer is fully drained.

With this, our RTL8139 NIC driver is basically complete. Next, let's implement the Ethernet frame handling logic to implement `ethernet_handler`.

### Ethernet Frames

An Ethernet frame consists of a destination MAC address, source MAC address, frame type, and payload — as introduced in the previous chapter.

Let's first implement a function to send an Ethernet frame:

```cpp
#include <kernel/net/ethernet.hpp>
#include <driver/rtl8139.hpp>
#include <kernel/mm.hpp>
#include <string.h>

typedef struct {
    char target_mac[6];
    char source_mac[6];
    char type[2];
} __attribute__((packed)) ethernet_head;

int send_ethernet_frame(char target_mac[6], char source_mac[6], char type[2],
    char* buffer, uint16_t size) {
    if (size > 1536 - sizeof(ethernet_head)) return -1;
    void* buf = kmalloc(sizeof(ethernet_head) + size);
    ethernet_head* head = (ethernet_head*)buf;
    memcpy(head->target_mac, target_mac, 6);
    memcpy(head->source_mac, source_mac, 6);
    memcpy(head->type, type, 2);
    memcpy(buf + sizeof(ethernet_head), buffer, size);
    return nic_write((const char*)buf, sizeof(ethernet_head) + size);
}
```

Sending is basically wrapping the data with parameters and calling the lower-level function. However, there's a drawback: each layer of encapsulation requires a `kmalloc`, which is immediately freed after use. This is costly in both time and memory. It would be better to reserve headroom from the start and fill in the header pointers gradually. We'll do that later.

The implementation of `ethernet_handler` is also simple: use `ethernet_head` to parse the header, and based on the type field, call the corresponding network layer protocol handler — such as IP, ICMP, and ARP (which we're about to implement).

```cpp
void arp_handler(char* buffer, uint16_t size);

void ethernet_handler(char* buffer, uint16_t size) {
    if (size < 60) return;
    char* type = reinterpret_cast<ethernet_head*>(buffer)->type;
    // Note: network transmission uses big endian, but since we check byte-by-byte, it's fine
    if (type[0] == 0x08 && type[1] == 0x06) { // ARP
        arp_handler(buffer + sizeof(ethernet_head), size - sizeof(ethernet_head));
    }
    return;
}
```

We'll leave `arp_handler` for later implementation.

### ARP

Finally, we've reached our main task: sending an ARP request and trying to receive a response.

#### ARP Introduction

ARP (Address Resolution Protocol) is used to discover the hardware address (e.g., MAC address) corresponding to a protocol address (e.g., IP address). Sending an ARP request broadcasts it to all devices on the network. The content is: "I am a device with protocol address XX and hardware address XX. I am looking for the hardware address of the device with protocol address YY under the same protocol and hardware type." When the corresponding hardware receives the request, it sends a response to the requesting hardware address, informing it of its own hardware address.

Let's look at the ARP packet structure:

![](../assets/自制操作系统（21）：rtl8139网卡驱动（下）- 中断处理与ARP支持/9998227288533573-00d7182c-0e19-4f71-8eb2-6ba0aba44c0f-image_task_01KJYXC01FH8KD4Q8W8ZSEPNCZ.jpg)

Now let's write code to handle various scenarios:

#### 1. I Want to Ask for Someone's MAC

Asking for someone's MAC essentially means sending an ARP packet:

```cpp
typedef struct {
    uint16_t hw_type;       // Hardware type, Ethernet = 0x0001
    uint16_t proto_type;    // Protocol type, IPv4 = 0x0800
    uint8_t  hw_len;        // Hardware address length, 6
    uint8_t  proto_len;     // Protocol address length, 4
    uint16_t opcode;        // 1 = request, 2 = reply
    uint8_t  sender_mac[6];
    uint8_t  sender_ip[4];
    uint8_t  target_mac[6];
    uint8_t  target_ip[4];
} __attribute__((packed)) arp_packet;
```

Except for the last two fields, everything before is fixed. Since we don't know the target MAC, we can fill it with all zeros. The target IP is the IP whose MAC we want to query:

```cpp
const uint8_t broadcast_mac[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

int send_arp(uint16_t opcode, uint8_t* target_mac, uint8_t* target_ip) {
    arp_packet header;
    header.hw_type = htons(0x0001);
    header.proto_type = htons(0x0800);
    header.hw_len = 6;
    header.proto_len = 4;
    header.opcode = opcode;
    memcpy(header.sender_mac, my_mac(), 6);
    memcpy(header.sender_ip, my_ip, 4);
    memcpy(header.target_mac, target_mac, 6);
    memcpy(header.target_ip, target_ip, 4);
    // Ethernet destination: request uses broadcast, reply uses the target MAC
    uint8_t* eth_dst = (opcode == htons(APR_OPCODE_REQ))
        ? (uint8_t*)broadcast_mac
        : target_mac;
    return send_ethernet_frame(eth_dst, my_mac(), TYPE_ARP, &header, sizeof(header));
}
```

We can wrap this into a function: `send_arp`.

A note about `htons`: it converts from the host byte order (little endian) to network byte order (big endian).

Also, let's set a fixed IP address for ourselves for now (we can implement automatic IP acquisition later):

```cpp
const uint8_t my_ip[] = {
    10, 0, 1, 1
};
```

After calling `send_arp`, if the corresponding host is found, it will send us a reply. This brings us to the second scenario: someone replying to me with their MAC.

#### 2. Someone Replies to Me with Their MAC

External ARP packets will eventually trigger our `arp_handler`. We need to do some initial filtering:

```cpp
void arp_handler(char* buffer, uint16_t size) {
    arp_packet* header = reinterpret_cast<arp_packet*>(buffer);
    // Only accept <IP, Ethernet> mappings
    if (header->hw_type != htons(0x0001) || header->proto_type != htons(0x0800)) return;
```

For the case where someone replies to me with their MAC, I need to record the IP->MAC mapping in my data structure:

```cpp
void arp_handler(char* buffer, uint16_t size) {
    arp_packet* header = reinterpret_cast<arp_packet*>(buffer);
    // Only accept <IP, Ethernet> mappings
    if (header->hw_type != htons(0x0001) || header->proto_type != htons(0x0800)) return;
    if (header->opcode == htons(APR_OPCODE_REQ)) { // Request
        if (is_same_ip(my_ip, header->target_ip)) { // Scenario 3: someone asks for MAC, and the target IP is mine
                send_arp(htons(APR_OPCODE_REPLY), header->sender_mac, header->sender_ip); // I'll respond
            }
    } else if (header->opcode == htons(APR_OPCODE_REPLY) && is_same_mac(my_mac(), header->target_mac)) {
        // Scenario 2: someone replies to me with their MAC
        arp_table_insert(header->sender_ip, header->sender_mac);
    }
}
```

I use a hash table here:

```cpp
#include <unordered_map>

std::unordered_map<uint32_t, uint64_t> ip_to_mac;

bool arp_table_lookup(uint8_t* ip, uint8_t* mac) {
    uint32_t t_ip = 0;
    for (int i = 0; i < 4; ++i) {
        t_ip = (t_ip << 8) + ip[4 - i - 1];
    }
    if (ip_to_mac.find(t_ip) == ip_to_mac.end()) return false;
    uint64_t t_mac = ip_to_mac[t_ip];
    for (int i = 0; i < 6; ++i) {
        mac[6 - i - 1] = t_mac & 0xff;
        t_mac >>= 8;
    }
    return true;
}

void arp_table_insert(uint8_t* ip, uint8_t* mac) {
    uint32_t t_ip = 0;
    for (int i = 0; i < 4; ++i) {
        t_ip = (t_ip << 8) + ip[4 - i - 1];
    }
    uint64_t t_mac = 0;
    for (int i = 0; i < 6; ++i) {
        t_mac = (t_mac << 8) + mac[6 - i - 1];
    }
    ip_to_mac[t_ip] = t_mac;
}
```

This way, we can use `arp_table_lookup` to find the MAC address corresponding to an IP, though we still need to convert the result ourselves.

#### 3. Someone Asks for My MAC

This was already shown in the code above:

```cpp
    if (header->opcode == htons(APR_OPCODE_REQ)) { // Request
        if (is_same_ip(my_ip, header->target_ip)) { // Scenario 3: someone asks for MAC, and the target IP is mine
                send_arp(htons(APR_OPCODE_REPLY), header->sender_mac, header->sender_ip); // I'll respond
            }
```

#### Testing

Let's test the MAC query scenario. But first, we need to assign an IP to our host on the tap interface:

```shell
 sudo ip addr add 10.0.1.0/24 dev tap0
```

![image-20260306002013002](../assets/自制操作系统（21）：rtl8139网卡驱动（下）- 中断处理与ARP支持/image-20260306002013002.png)

Prepare some functions:

```cpp
void to_print_mac(uint8_t* ip) {
    printf("sending arp request...\n");
    send_arp(htons(APR_OPCODE_REQ), broadcast_mac, ip);
}
```

Print the new table entry:

```cpp
void arp_table_insert(uint8_t* ip, uint8_t* mac) {
    uint32_t t_ip = 0;
    for (int i = 0; i < 4; ++i) {
        t_ip = (t_ip << 8) + ip[4 - i - 1];
    }
    uint64_t t_mac = 0;
    for (int i = 0; i < 6; ++i) {
        t_mac = (t_mac << 8) + mac[6 - i - 1];
    }
    ip_to_mac[t_ip] = t_mac;
    printf("new arp record:\n");
    printf("ip: %d:%d:%d:%d\n", ip[0], ip[1], ip[2], ip[3]);
    printf("mac: %X:%X:%X:%X:%X:%X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
```

Call our ARP request function in the kernel:

```cpp
    uint8_t ip[4] = {10, 0, 1, 0};
    printf("ip to get: %d:%d:%d:%d\n", ip[0], ip[1], ip[2], ip[3]);
    to_print_mac(ip);
```

![image-20260306003325246](../assets/自制操作系统（21）：rtl8139网卡驱动（下）- 中断处理与ARP支持/image-20260306003325246.png)

Running... no record returned. Why?

It turns out: 1. I had previously set up PIC slave offsets and forgot to account for them when registering the interrupt function; 2. The slave PIC wasn't enabled.

```cpp
void reg_isr() {
    // Read the NIC's interrupt number
    uint8_t irq = read_pci_by_32bits(pci_bus, pci_dev, pci_func, 15) & 0xFF;
    register_interrupt_handler(irq + 0x20, rtl8139_interrupt_handler); // Add slave offset, subtract master's 7 devices

    hal_enable_irq(2);   // Enable slave PIC
    hal_enable_irq(irq); // Enable IRQ 11 itself

    // Enable receive OK and receive error interrupts
    outw(io_addr + REG_IMR, 0x0001 | 0x0004);  // ROK | RXErr
}
```

![image-20260306004536178](../assets/自制操作系统（21）：rtl8139网卡驱动（下）- 中断处理与ARP支持/image-20260306004536178.png)

Now it can respond to interrupts, but still no MAC is printed. Tracing up the call chain:

![image-20260306005203228](../assets/自制操作系统（21）：rtl8139网卡驱动（下）- 中断处理与ARP支持/image-20260306005203228.png)

tcpdump confirms our request was sent successfully.

```cpp
void ethernet_handler(char* buffer, uint16_t size) {
    if (size < sizeof(ethernet_head)) return;
    char* type = reinterpret_cast<ethernet_head*>(buffer)->type;
    // Note: network transmission uses big endian, but since we check byte-by-byte, it's fine
    if (type[0] == 0x08 && type[1] == 0x06) { // ARP
        arp_handler(buffer + sizeof(ethernet_head), size - sizeof(ethernet_head));
    }
    return;
}
```

After reducing the minimum Ethernet frame size threshold, we could receive them.

![image-20260306005555796](../assets/自制操作系统（21）：rtl8139网卡驱动（下）- 中断处理与ARP支持/image-20260306005555796.png)

But we still occasionally miss some:

![image-20260306010634618](../assets/自制操作系统（21）：rtl8139网卡驱动（下）- 中断处理与ARP支持/image-20260306010634618.png)

The interrupt is there, but the buffer is empty.

```cpp
    if (irq + 0x20 >= 0x28) {
        outb(0xA0, 0x20);  // slave EOI
    }
    outb(0x20, 0x20);      // master EOI
```

I asked Claude, and suddenly realized: in my interrupt handler, I wasn't marking the interrupt as handled! This meant the next interrupt wouldn't be sent. Sometimes we'd receive a "transmission successful" interrupt first, but the subsequent interrupt notifying us of buffer content couldn't get through.

![image-20260306011459117](../assets/自制操作系统（21）：rtl8139网卡驱动（下）- 中断处理与ARP支持/image-20260306011459117.png)

Now it works normally.

![image-20260306012415729](../assets/自制操作系统（21）：rtl8139网卡驱动（下）- 中断处理与ARP支持/image-20260306012415729.png)

And after testing, our system can correctly respond to ARP requests. Applause! 👏

---

### Summary

We have implemented ARP support. In the next section, we'll implement IP and ICMP protocol support, and see how to make our system capable of pinging and responding to pings!
