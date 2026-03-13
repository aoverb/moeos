#include <stdint.h>
#include <stdio.h>
#include <syscall_def.hpp>
#if defined(__is_libk)
#include <kernel/tty.h>
#else
#include <file.h>
#endif

void tcsetpgrp(int fd, pid_t pid) {
    syscall2((uint32_t)SYSCALL::TCSETPGRP, (uint32_t)fd, (uint32_t)pid);
}

void cls() {
    #if defined(__is_libk)
        terminal_clear();
    #else
        syscall0((uint32_t)SYSCALL::TERMINAL_CLEAR);
    #endif
}

#if !defined(__is_libk)
static char readbuf[512];
static int  readbuf_pos = 0;
static int  readbuf_len = 0;

static int read_one_byte(char* c) {
    if (readbuf_pos >= readbuf_len) {
        readbuf_len = read(0, readbuf, sizeof(readbuf));
        readbuf_pos = 0;
        if (readbuf_len <= 0) return -1; // EOF
    }
    *c = readbuf[readbuf_pos++];
    return 0;
}
#endif

bool getline(char* buf, uint32_t size) {
#if defined(__is_libk)
    uint32_t i = 0;

    while (i < size - 1) {
        terminal_flush();
        int n = terminal_read_char();
        if (n < 0) {
            buf[i] = '\0';
            return false;
        }

        char c = (char)n;

        if (c == '\b') {
            if (i == 0) continue;
            --i;
            printf("\b");
            continue;
        }

        if (c == '\n') {
            buf[i] = '\0';
            printf("\n");
            return true;
        }

        if (c >= 32 && c <= 126) {
            buf[i++] = c;
            printf("%c", c);
        }
    }

    buf[i] = '\0';
    return true;
#else
    uint32_t i = 0;
    char c;
    while (i < size - 1) {
        if (read_one_byte(&c) < 0) {
            if (i == 0) { buf[0] = '\0'; return false; }
            break;
        }
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return true;
#endif
}