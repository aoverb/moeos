#include <stdint.h>
#include <stdio.h>
#include <syscall_def.h>
#if defined(__is_libk)
#include <driver/keyboard.h>
#endif


void getline(char* buf, uint32_t size) {
#if defined(__is_libk)
    keyboard_flush();
    uint32_t i = 0;

    while (i < size - 1) {
        while (!keyboard_haschar()) {
            asm volatile("pause"); 
        }

        char c = keyboard_getchar();

        if (c == '\b') {
            if (i == 0) continue;
            --i;
            printf("\b");
            continue;
        }

        if (c == '\n') {
            buf[i] = '\0';
            printf("\n");
            return;
        }

        if (c >= 32 && c <= 126) {
            buf[i++] = c;
            printf("%c", c);
        }
    }

    buf[i] = '\0';
#else
	// TODO: Implement stdio and the write system call.
	syscall2(3, reinterpret_cast<uint32_t>(buf), size);
#endif

}