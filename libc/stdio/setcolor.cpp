#include <stdio.h>
#include <syscall_def.hpp>
#if defined(__is_libk)
#include <kernel/tty.h>
#endif

void set_color(uint32_t color) {
#if defined(__is_libk)
	terminal_setcolor(color);
#else
	syscall1(2, color);
#endif
}