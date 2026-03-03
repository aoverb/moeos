#include <stdint.h>
#include <kernel/tty.h>
#include <driver/keyboard.h>
#include <driver/devfs.hpp>

static int console_read(char* buffer, uint32_t offset, uint32_t size) {
    uint32_t i = 0;
    while (i < size) {
        while (!keyboard_haschar())
            asm volatile("pause");
        char c = keyboard_getchar();
        buffer[i++] = c;
        if (c == '\n') break;
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