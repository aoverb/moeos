#include <stdio.h>

#if defined(__is_libk)
#include <kernel/tty.h>
#else
#include <file.h>
#endif

#include <syscall_def.h>

int putchar(int ic) {
#if defined(__is_libk)
	char c = (char) ic;
	terminal_write(&c, sizeof(c));
#else
	char c = (char) ic;
	char s[2];
	s[0] = c;
	s[1] = '\0';
	write(1, s, 1);
#endif
	return ic;
}