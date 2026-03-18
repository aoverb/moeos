#include <stdint.h>
#include <termios.h>
#include <kernel/tty.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <driver/devfs.hpp>
static char line_buf[256];
static uint32_t line_len = 0;
static bool line_ready = false;

static termios next_setting;
static uint8_t has_next_setting_flag = 0; // 2代表丢弃所有输入

static termios setting;
static int console_peek_() {
    return terminal_read_char_for_peek();
}

static int console_peek() {
    while (!line_ready) {
        int n = terminal_read_char();
        if (n < 0) {
            line_len = 0;
            line_ready = false;
            return 0;
        }

        char c = (char)n;
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
    if (has_next_setting_flag > 1) {
        setting = next_setting;
        memcpy(setting.c_cc, next_setting.c_cc, NCCS);
        if (has_next_setting_flag == 2) {
            terminal_flush();
        }
        has_next_setting_flag = 0;
    }
    return size;
}

static int console_ioctl(uint32_t request, void* arg) {
    if (!arg) return -EFAULT;
    switch (request)
    {
    case TCGETS:
        setting = *(termios*)arg;
        break;
    case TCSETS:
        next_setting = *(termios*)arg;
        has_next_setting_flag = 1;
        break;
    case TCSETSW:
        next_setting = *(termios*)arg;
        has_next_setting_flag = 2;
        break;
    case TCSETSF:
        next_setting = *(termios*)arg;
        terminal_flush();
        break;
    case TIOCGWINSZ:
        terminal_getwinsize(*(winsize*)arg);
        break;
    default:
        break;
    }
    return 0;
}

void init_console_dev(mounting_point* mp) {
    static dev_operation ops = {};
    ops.read  = console_read;
    ops.write = console_write;
    ops.peek = console_peek_;
    ops.ioctl = console_ioctl;
    register_in_devfs(mp, "console", &ops);
}