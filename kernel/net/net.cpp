#include <kernel/net/net.hpp>

uint16_t checksum(void* data, uint32_t size) {
    uint32_t res = 0;
    for (int i = 0; i < size / 2; ++i) { // 每次算16位，两个字节
        res += *((uint16_t*)data + i);
    }
    if (size % 2 != 0) {
        res += *((uint8_t*)data + size - 1);
    }
    while (res >> 16) {
        res = (res & 0xFFFF) + (res >> 16); // 折叠进位
    }
    return ~(uint16_t)res;
}

static netconf net_conf = {
    {"10", "0", "1", "1"},
    {"255", "255", "255", "0"},
    {"0", "0", "0", "0", "0", "0"}
};

extern "C" void get_mac(uint8_t mac[6]);

void init_netconf() {
    uint8_t mac[6];
    get_mac(mac);
    net_conf.mac = mac;
}

const netconf* getLocalNetconf() {
     return &net_conf; 
}
