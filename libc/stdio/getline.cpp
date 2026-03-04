#include <stdint.h>
#include <stdio.h>
#include <syscall_def.hpp>
#if defined(__is_libk)
#include <driver/keyboard.h>
#include <kernel/tty.h>
#else
#include <file.h>
#endif

void cls() {
    #if defined(__is_libk)
        terminal_clear();
    #else
        syscall0((uint32_t)SYSCALL::TERMINAL_CLEAR);
    #endif
}

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
    int n = read(0, buf, size - 1);
    if (n < 0) n = 0;
    if (n > 0 && buf[n - 1] == '\n') n--;
    buf[n] = '\0';
#endif

}