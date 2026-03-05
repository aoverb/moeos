#ifndef _DRIVER_RTL8139_H
#define _DRIVER_RTL8139_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rtl8139_init();

int nic_read(char* buffer, uint32_t /* offset */, uint32_t size);
int nic_write(const char* buffer, uint32_t size);
void get_mac(uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif