#include <kernel/net/tcp.hpp>
#include <kernel/mm.h>
#include <format.h>

struct TCB { // 传输控制块
    tcb_state state;
    char* window;
    size_t window_size;
};

constexpr uint32_t DEFAULT_WINDOW_SIZE = (1 << 16) - 1;
constexpr uint32_t DEFAULT_WINDOW_SCALE = 1;

int tcp_init(socket& sock, uint16_t local_port) {
    sock.ptcl = protocol::TCP;
    strcpy("127.0.0.1", sock.dst_addr); // 先给一个默认地址，后面通过connect设置
    sock.dst_port = 0; // 与上面同理

    uint8_t src_addr[4];
    getLocalNetconf()->ip.to_bytes(src_addr);
    sprintf(sock.src_addr, "%d.%d.%d.%d", src_addr[0], src_addr[1], src_addr[2], src_addr[3]);
    sock.src_port = local_port;

    sock.data = kmalloc(sizeof(TCB));
    TCB* tcb = (TCB*)sock.data;
    tcb->state = tcb_state::CLOSED;
    tcb->window_size = DEFAULT_WINDOW_SIZE * DEFAULT_WINDOW_SCALE;
    tcb->window = (char*)kmalloc(tcb->window_size);
    return 1;
}

int tcp_connect(socket& sock, const char* addr, uint16_t port) {
    // SEND SYN
    void* header = kmalloc(sizeof(pseudo_tcp_header) + sizeof(tcp_header));
    memset(header, 0, sizeof(pseudo_tcp_header) + sizeof(tcp_header));
    pseudo_tcp_header* p_header = (pseudo_tcp_header*)header;
    tcp_header* t_header = (tcp_header*)((char*)header + sizeof(pseudo_tcp_header));
    TCB* tcb = (TCB*)sock.data;
    int tmp[4];

    sscanf_s(addr, "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
    uint8_t dst_addr[4] = { (uint8_t)tmp[0], (uint8_t)tmp[1], (uint8_t)tmp[2], (uint8_t)tmp[3] };

    sscanf_s(sock.src_addr, "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
    uint8_t src_addr[4] = { (uint8_t)tmp[0], (uint8_t)tmp[1], (uint8_t)tmp[2], (uint8_t)tmp[3] };
    p_header->src_addr = ipv4addr(src_addr).addr;
    p_header->dst_addr = ipv4addr(dst_addr).addr;
    p_header->protocol = IP_PROTOCOL_TCP;
    p_header->zero = 0;
    p_header->tcp_length = htons(sizeof(tcp_header));

    t_header->src_port = htons(sock.src_port);
    t_header->dst_port = htons(port);
    t_header->seq_num = 0;
    t_header->ack_num = 0; // todo: 这里要使用时间戳+随机数生成
    t_header->reserved = 0;
    t_header->data_offset = sizeof(tcp_header) / 4;
    t_header->flags = 0;
    t_header->flags = (uint8_t)tcp_flags::SYN;
    t_header->window = htons(tcb->window_size);
    t_header->checksum = 0;
    t_header->urgent_ptr = 0;

    t_header->checksum = checksum(header, sizeof(pseudo_tcp_header) + sizeof(tcp_header));

    send_ipv4((ipv4addr(dst_addr)), IP_PROTOCOL_TCP, t_header, sizeof(tcp_header));
    kfree(header);
    return 0;
}

void tcp_handler(uint16_t ip_header_size, char* buffer, uint16_t size) {
    printf("got a tcp packet! size: %d\n", size);
}