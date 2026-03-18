#ifndef _ARCH_I386_SIGNAL_H
#define _ARCH_I386_SIGNAL_H
#include <stdint.h>
#include <signal.h>
#ifdef __cplusplus
extern "C" {
#endif

void signal_init();
bool send_signal(pid_t pid, uint32_t sig);
void register_signal(uint8_t n, signal_handler_t handler);

#ifdef __cplusplus
}
#endif

#endif