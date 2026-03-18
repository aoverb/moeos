#ifndef _KERNEL_TTY_H
#define _KERNEL_TTY_H

#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <termios.h>

/* 如果是 C++ 环境，告诉编译器这部分按 C 的链接规则处理 */
#ifdef __cplusplus
extern "C" {
#endif
using pid_t = int;
struct multiboot_info_t;

void terminal_input(char c);
void terminal_initialize(struct multiboot_info_t* mbi);
void terminal_draw_char(int x, int y, const uint8_t* font_char, uint32_t color);
void terminal_setcolor(uint32_t color);
void terminal_write(const char* data, size_t size);
void terminal_fill_rect(int x, int y, int width, int height, uint32_t color);
void terminal_clear();
int terminal_read_char();
void terminal_flush();
void terminal_setforeground(pid_t pid);
int terminal_read_char_for_peek();
void terminal_set_read_wait_time(uint32_t ms);
const termios& terminal_get_setting();
void terminal_apply_setting(const termios& w);
void terminal_getwinsize(winsize& w);
#ifdef __cplusplus
}
#endif

#endif
