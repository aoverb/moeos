#include <kernel/net/net.hpp>
#include <driver/devfs.hpp>
#include <format.h>

static netconf net_conf;

extern "C" void get_mac(uint8_t mac[6]);

static int ipv4_addr_read(char* buffer, uint32_t, uint32_t size) {
    uint8_t out[4];
    net_conf.ip.to_bytes(out);
    char output[16];
    sprintf(output, "%d.%d.%d.%d", out[0], out[1], out[2], out[3]);
    strncpy(buffer, output, size < strlen(output) ? size : strlen(output));
    return size < strlen(output) ? size : strlen(output);
}

static int ipv4_addr_write(const char*, uint32_t) { return -1; } // 不支持改IP

void init_ipv4addr_dev_file(mounting_point* mp) {
    static dev_operation ipv4_addr_opr;
    ipv4_addr_opr.read = &ipv4_addr_read;
    ipv4_addr_opr.write = &ipv4_addr_write;
    register_in_devfs(mp, "ipv4_addr", &ipv4_addr_opr);
}

void init_netconf() {
    uint8_t mac[6];
    get_mac(mac);
    net_conf.mac = mac;
    net_conf.ip = ipv4addr("10", "0", "1", "1");
    net_conf.mask = ipv4addr("255", "255", "255", "0");
}

const netconf* getLocalNetconf() {
     return &net_conf;
}
