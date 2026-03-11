#include <stdint.h>
#include <kernel/tty.h>
#include <driver/keyboard.h>
#include <driver/devfs.hpp>
static char line_buf[256];
static uint32_t line_len = 0;
static bool line_ready = false;

static int console_peek() {
    while (keyboard_haschar() && !line_ready) {
        char c = keyboard_getchar();

        if (c == '\b') {
            if (line_len > 0) {
                --line_len;
                terminal_write("\b", 1);   // 回显退格
            }
            continue;
        }

        if (c == '\n') {
            line_buf[line_len++] = '\n';
            terminal_write("\n", 1);   // 回显换行
            line_ready = true;
            break;
        }

        if (c >= 32 && c <= 126 && line_len < sizeof(line_buf) - 1) {
            line_buf[line_len++] = c;
            terminal_write(&c, 1);     // 回显可见字符
        }
    }
    return line_ready ? (int)line_len : 0;
}

static int console_read(char* buffer, uint32_t offset, uint32_t size) {
    while (!line_ready) {
        console_peek();
        if (!line_ready)
            asm volatile("pause");
    }

    uint32_t n = line_len < size ? line_len : size;
    memcpy(buffer, line_buf, n);

    // 重置缓冲区
    line_len = 0;
    line_ready = false;
    return n;
}

static int console_write(const char* buffer, uint32_t size) {
    terminal_write(buffer, size);
    return size;
}

void init_console_dev(mounting_point* mp) {
    static dev_operation ops = {};
    ops.read  = console_read;
    ops.write = console_write;
    ops.peek = console_peek;
    register_in_devfs(mp, "console", &ops);
}