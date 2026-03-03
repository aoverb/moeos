#include <stdint.h>
#include <kernel/tty.h>
#include <driver/keyboard.h>
#include <driver/devfs.hpp>

static int console_read(char* buffer, uint32_t offset, uint32_t size) {
    uint32_t i = 0;
    while (i < size - 1) {
        while (!keyboard_haschar())
            asm volatile("pause");
        char c = keyboard_getchar();

        if (c == '\b') {
            if (i == 0) continue;
            --i;
            terminal_write("\b", 1);   // 回显退格
            continue;
        }

        if (c == '\n') {
            buffer[i++] = '\n';
            terminal_write("\n", 1);   // 回显换行
            break;
        }

        if (c >= 32 && c <= 126) {
            buffer[i++] = c;
            terminal_write(&c, 1);     // 回显可见字符
        }
    }
    return i;
}

static int console_write(const char* buffer, uint32_t size) {
    terminal_write(buffer, size);
    return size;
}

void init_console_dev(mounting_point* mp) {
    static dev_operation ops = {};
    ops.read  = console_read;
    ops.write = console_write;
    register_in_devfs(mp, "console", &ops);
}