#ifndef _KERNEL_TTY_H
#define _KERNEL_TTY_H

#include <stddef.h>
#include <stdint.h>

/* 如果是 C++ 环境，告诉编译器这部分按 C 的链接规则处理 */
#ifdef __cplusplus
extern "C" {
#endif

struct multiboot_info_t;

void terminal_initialize(struct multiboot_info_t* mbi);
void terminal_setcolor(uint32_t color);
void terminal_write(const char* data, size_t size);
#ifdef __cplusplus
}
#endif

#endif
