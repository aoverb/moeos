#ifndef _KERNEL_NET_ARP_H
#define _KERNEL_NET_ARP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int send_ethernet_frame(char target_mac[6], char source_mac[6], char type[2],
    char* buffer, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif