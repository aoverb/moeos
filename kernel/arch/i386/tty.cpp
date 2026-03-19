/* kernel/arch/i386/tty.cpp */
#include <kernel/tty.h>
#include <kernel/font.h>
#include <kernel/mm.hpp>
#include <kernel/process.hpp>
#include <kernel/ksignal.h>
#include <kernel/schedule.hpp>
#include <kernel/timer.hpp>
#include <driver/devfs.hpp>
#include <string.h>
#include <boot/multiboot.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h> // for uint8_t etc.

extern uint32_t page_directory;
spinlock tty_lock;



static pid_t foreground_pid = 4;
static uint32_t* fb_addr;
static uint32_t fb_pitch;
static uint32_t fb_bpp;
static uint32_t terminal_cols;  // 字符列数
static uint32_t terminal_rows;  // 字符行数
uint32_t fb_width;   // 屏幕宽度（像素）
uint32_t fb_height;  // 屏幕高度（像素）
static uint32_t terminal_row = 0;
static uint32_t terminal_col = 0;

static uint32_t terminal_color = 0x00FFFFFF;
static uint32_t terminal_bg_color = 0x00000000;
static bool terminal_reverse = false;

static process_queue tty_wait_queue = nullptr;

static termios setting;
static bool show_cursor = true;
static uint32_t read_wait_time = 100;

const termios& terminal_get_setting() {
    return setting;
}
void terminal_apply_setting(const termios& s) {
    setting = s;
}

void map_lfb(uint32_t phys_addr, uint32_t size) {
    phys_addr &= ~((1 << 12) - 1);
    uintptr_t vram_addr_begin = 0xD0000000;
    uintptr_t num_pages = (size + 0xFFF) / 0x1000;
    
    for (uintptr_t i = 0; i < num_pages; ++i)
        vmm_map_page(phys_addr + i * (1 << 12), vram_addr_begin + i * (1 << 12), 0x3);
}

struct round_buffer {
    volatile char buffer[256];
    volatile uint8_t head = 0;
    volatile uint8_t tail = 0;
};

static round_buffer kbd_buffer;

void rb_flush() {
    kbd_buffer.head = kbd_buffer.tail = 0;
}

char rb_read() {
    if (kbd_buffer.head == kbd_buffer.tail) {
        return -1;
    }
    char re = kbd_buffer.buffer[kbd_buffer.head++];
    return re;
}

void rb_write(char data) {
    kbd_buffer.buffer[(kbd_buffer.tail)++] = data;
    if (kbd_buffer.head == kbd_buffer.tail) {
        ++(kbd_buffer.head);
    }
}

void terminal_flush() {
    SpinlockGuard guard(tty_lock);
    rb_flush();
}

void terminal_input(char c) {
    if (c == 0x03 && (setting.c_lflag & ISIG)) {
        terminal_write("^C\n", 3);
        if (foreground_pid == 0) return;
        send_signal(foreground_pid, SIGINT);
        {
            SpinlockGuard ttyguard(tty_lock);
            SpinlockGuard guard(process_list_lock);
            if (process_list[foreground_pid]->inwait_queue &&
               *(process_list[foreground_pid]->inwait_queue)) {
                remove_from_waiting_queue(*(process_list[foreground_pid]->inwait_queue), foreground_pid);
                process_list[foreground_pid]->state = process_state::READY;
                insert_into_scheduling_queue(foreground_pid);
            }
        }
        return;
    }
    {
        SpinlockGuard ttyguard(tty_lock);
        rb_write(c);
        {
            SpinlockGuard guard(process_list_lock);
            PCB* cur;
            while(cur = tty_wait_queue) {
                remove_from_waiting_queue(tty_wait_queue, cur->pid);
                cur->state = process_state::READY;
                insert_into_scheduling_queue(cur->pid);
            }
        }
    }
}

void terminal_setforeground(pid_t pid) {
    SpinlockGuard guard(tty_lock);
    foreground_pid = pid;
}

void terminal_getwinsize(winsize& w) {
    w.ws_col = terminal_cols;
    w.ws_row = terminal_rows;
    w.ws_xpixel = fb_width;
    w.ws_ypixel = fb_height;
}

void terminal_initialize(multiboot_info_t* mbi) {
    fb_addr = (uint32_t*)(uintptr_t)mbi->framebuffer_addr;
    fb_pitch = mbi->framebuffer_pitch;
    fb_bpp = mbi->framebuffer_bpp;

    /* 从 multiboot 获取分辨率 */
    fb_width = mbi->framebuffer_width;
    fb_height = mbi->framebuffer_height;
    
    /* 计算字符行列数 */
    terminal_cols = fb_width / FONT_WIDTH;
    terminal_rows = fb_height / FONT_HEIGHT;


    uint32_t lfb_physical_addr = mbi->framebuffer_addr;
    uint32_t lfb_size = mbi->framebuffer_pitch * mbi->framebuffer_height;
    map_lfb(lfb_physical_addr, lfb_size);
    fb_addr = (uint32_t*)(uintptr_t)0xD0000000;

    setting.c_lflag |= (ECHO | ICANON | ISIG);
}

void terminal_setcolor(uint32_t color) {
    terminal_color = color;
}

void terminal_putpixel(int x, int y, uint32_t color) {
    uint32_t offset = y * (fb_pitch / 4) + x;
    fb_addr[offset] = color;
}


void terminal_draw_char(int x, int y, const uint8_t* font_char, uint32_t fg, uint32_t bg) {
    // 控制台绘制字体的逻辑不应该依赖于字体的具体实现！需要重构。但是现在只是用来简单输出字符，刚刚好够用。
    for (int row = 0; row < FONT_HEIGHT; row++) {
        for (int col = 0; col < FONT_WIDTH; col++) {
            if (font_char[row] & (0x80 >> col)) {
                terminal_putpixel(x + col, y + row, fg);
            } else {
                terminal_putpixel(x + col, y + row, bg);
            }
        }
    }
}

void terminal_fill_rect(int x, int y, int width, int height, uint32_t color) {
    for(int i = y; i < y + height; i++) {
        for(int j = x; j < x + width; j++) {
            terminal_putpixel(j, i, color);
        }
    }
}

void terminal_scroll() {
    size_t line_size = fb_pitch;
    
    for (uint32_t row = 1; row < terminal_rows; row++) {
        uint8_t* src = (uint8_t*)fb_addr + row * FONT_HEIGHT * line_size;
        uint8_t* dst = (uint8_t*)fb_addr + (row - 1) * FONT_HEIGHT * line_size;
        
        for (int i = 0; i < FONT_HEIGHT; i++) {
            memcpy(dst + i * line_size, src + i * line_size, line_size);
        }
    }
    
    // 清空最后一行
    uint32_t bg = terminal_reverse ? terminal_color : terminal_bg_color;
    terminal_fill_rect(0, (terminal_rows - 1) * FONT_HEIGHT, 
                      fb_width, FONT_HEIGHT, bg);
    
    terminal_row = terminal_rows - 1;
}

const uint8_t NORMAL = 0;
const uint8_t ESC = 1;
const uint8_t CSI = 2;
const uint8_t CSI_PRIV = 3;
static uint8_t state = 0;

constexpr uint8_t PARAMS_LENGTH = 4;
static char param[16];
static uint8_t param_len = 0;
static uint32_t params[PARAMS_LENGTH];
static uint8_t params_idx = 0;
void terminal_write(const char* data, size_t size) {
    SpinlockGuard guard(tty_lock);
    for (size_t i = 0; i < size; i++) {
        uint32_t fg = terminal_reverse ? terminal_bg_color : terminal_color;
        uint32_t bg = terminal_reverse ? terminal_color : terminal_bg_color;
        if (state == ESC) {
            if (data[i] == '[') {
                state = CSI;
                continue;
            }
            state = NORMAL;
            continue;
        }

        if (state == CSI) {
            if (isdigit(data[i])) {
                param[param_len++] = data[i];
            } else if (data[i] == ';') { // 多个参数
                param[param_len] = '\0';
                params[params_idx++ % PARAMS_LENGTH] = atoi(param);
                param_len = 0;
            } else if (data[i] == '?') { // CSI_PRIV模式
                param_len = 0;
                memset(params, 0, sizeof(params));
                state = CSI_PRIV;
            } else if (data[i] == 'H') { // 设置光标位置
                param[param_len] = '\0';
                if (param_len > 0) params[params_idx++] = atoi(param);
                
                // one-based
                int row = (params_idx > 0 && params[0] > 0) ? params[0] - 1 : 0;
                int col = (params_idx > 1 && params[1] > 0) ? params[1] - 1 : 0;
                
                if (row >= (int)terminal_rows) row = terminal_rows - 1;
                if (col >= (int)terminal_cols) col = terminal_cols - 1;
                
                terminal_row = row;
                terminal_col = col;
                param_len = 0;
                params_idx = 0;
                state = NORMAL;
                continue;
            } else if (data[i] == 'J') { // 清屏
                param[param_len] = '\0';
                int mode = param_len > 0 ? atoi(param) : 0;
                if (mode == 2) {
                    terminal_fill_rect(0, 0, fb_width, fb_height, bg);
                    terminal_row = 0;
                    terminal_col = 0;
                    param_len = 0;
                    state = NORMAL;
                    continue;
                }
                // 清除光标到屏幕末尾
                terminal_fill_rect(terminal_col * FONT_WIDTH,
                                terminal_row * FONT_HEIGHT,
                                fb_width - terminal_col * FONT_WIDTH,
                                FONT_HEIGHT, bg); // 先到行尾
                if (terminal_row + 1 < terminal_rows) {
                    terminal_fill_rect(0,
                                    (terminal_row + 1) * FONT_HEIGHT,
                                    fb_width,
                                    fb_height - (terminal_row + 1) * FONT_HEIGHT, bg); // 再到屏幕末尾
                }
                param_len = 0;
                state = NORMAL;
                continue;
            } else if (data[i] == 'K') { // 清除光标到行尾
                terminal_fill_rect(terminal_col * FONT_WIDTH,
                                terminal_row * FONT_HEIGHT,
                                fb_width - terminal_col * FONT_WIDTH,
                                FONT_HEIGHT, bg);
                param_len = 0;
                state = NORMAL;
                continue;
            } else if (data[i] == 'm') {
                param[param_len] = '\0';
                if (param_len > 0) params[params_idx++] = atoi(param);

                // 无参数等价于 0（重置）
                if (params_idx == 0) {
                    params[0] = 0;
                    params_idx = 1;
                }

                for (int p = 0; p < params_idx; p++) {
                    switch (params[p]) {
                    case 0:  // 重置
                        terminal_color = 0x00FFFFFF;
                        terminal_bg_color = 0x00000000;
                        terminal_reverse = false;
                        break;
                    case 1:  // 加粗，先忽略
                        break;
                    case 7:  // 反显
                        terminal_reverse = true;
                        break;
                    case 27: // 取消反显
                        terminal_reverse = false;
                        break;
                    // 标准色 (30-37)
                    case 30: terminal_color = 0x00414868; break; // 黑（深蓝灰）
                    case 31: terminal_color = 0x00F7768E; break; // 红（樱花粉红）
                    case 32: terminal_color = 0x009ECE6A; break; // 绿（抹茶绿）
                    case 33: terminal_color = 0x00E0AF68; break; // 黄（琥珀）
                    case 34: terminal_color = 0x007AA2F7; break; // 蓝（钴蓝）
                    case 35: terminal_color = 0x00BB9AF7; break; // 洋红（薰衣草紫）
                    case 36: terminal_color = 0x007DCFFF; break; // 青（天空蓝）
                    case 37: terminal_color = 0x00A9B1D6; break; // 白（月光灰）
                    case 39: terminal_color = 0x00C0CAF5; break; // 默认前景

                    // 标准背景色 (40-49)
                    case 40: terminal_bg_color = 0x001A1B26; break;
                    case 41: terminal_bg_color = 0x00F7768E; break;
                    case 42: terminal_bg_color = 0x009ECE6A; break;
                    case 43: terminal_bg_color = 0x00E0AF68; break;
                    case 44: terminal_bg_color = 0x007AA2F7; break;
                    case 45: terminal_bg_color = 0x00BB9AF7; break;
                    case 46: terminal_bg_color = 0x007DCFFF; break;
                    case 47: terminal_bg_color = 0x00A9B1D6; break;
                    case 49: terminal_bg_color = 0x00000000; break;

                    // 亮色前景 (90-97)
                    case 90: terminal_color = 0x00565F89; break; // 亮黑（注释灰）
                    case 91: terminal_color = 0x00FF9E9E; break; // 亮红
                    case 92: terminal_color = 0x00C3E88D; break; // 亮绿
                    case 93: terminal_color = 0x00FFD580; break; // 亮黄
                    case 94: terminal_color = 0x0082AAFF; break; // 亮蓝
                    case 95: terminal_color = 0x00C3A6FF; break; // 亮洋红
                    case 96: terminal_color = 0x00B4F9F8; break; // 亮青
                    case 97: terminal_color = 0x00C0CAF5; break; // 亮白
                    default: break;
                    }
                }

                param_len = 0;
                params_idx = 0;
                state = NORMAL;
                continue;
            } else {
                param_len = 0;
                state = NORMAL;
            }
            continue;
        }

        if (state == CSI_PRIV) {
            if (isdigit(data[i])) {
                param[param_len++] = data[i];
            } else if (data[i] == 'l') {
                param[param_len] = '\0';
                params[params_idx % PARAMS_LENGTH] = atoi(param);
                if (params[params_idx % PARAMS_LENGTH] == 25) {
                    show_cursor = false;
                    if (terminal_row < terminal_rows && terminal_col < terminal_cols)
                        terminal_fill_rect(terminal_col * FONT_WIDTH,
                                        terminal_row * FONT_HEIGHT,
                                        FONT_WIDTH, FONT_HEIGHT, bg);
                }
                param_len = 0;
                state = NORMAL;
            } else if (data[i] == 'h') {
                param[param_len] = '\0';
                params[params_idx % PARAMS_LENGTH] = atoi(param);
                if (params[params_idx % PARAMS_LENGTH] == 25) {
                    show_cursor = true;
                    if (terminal_row < terminal_rows && terminal_col < terminal_cols)
                        terminal_fill_rect(terminal_col * FONT_WIDTH,
                                        terminal_row * FONT_HEIGHT,
                                        FONT_WIDTH, FONT_HEIGHT, fg);
                }
                param_len = 0;
                state = NORMAL;
            } else {
                param_len = 0;
                state = NORMAL;
            }
            continue;
        }

        if (data[i] == '\x1b') {
            state = ESC;
            params_idx = 0;
            memset(params, 0, sizeof(params));
            continue;
        }

        if (show_cursor && terminal_col < terminal_cols && terminal_col >= 0 && terminal_row < terminal_rows && terminal_row >= 0)
            terminal_fill_rect(terminal_col * FONT_WIDTH, 
                            terminal_row * FONT_HEIGHT, 
                            FONT_WIDTH, 
                            FONT_HEIGHT, 
                            bg);
        if (data[i] == '\r') {
            terminal_col = 0;
            continue;
        }
        if (data[i] == '\b') {
            if (terminal_col == 0 && terminal_row == 0) {
                return;
            }
            if (terminal_col == 0) {
                --terminal_row;
                terminal_col = terminal_cols;
            }
            --terminal_col;
            terminal_fill_rect(terminal_col * FONT_WIDTH, 
                              terminal_row * FONT_HEIGHT, 
                              FONT_WIDTH, 
                              FONT_HEIGHT, 
                              show_cursor ? fg : bg);
            continue;
        }
        if (data[i] == '\n') {
            ++terminal_row;
            terminal_col = 0;
            if (terminal_row >= terminal_rows)
                terminal_scroll();
            continue;
        }
        if (data[i] == '\t') {
            uint32_t spaces = 8 - (terminal_col % 8);
            terminal_col += spaces;
            if (terminal_col >= terminal_cols) {
                terminal_col = 0;
                ++terminal_row;
            }
            if (terminal_row >= terminal_rows)
                terminal_scroll();
            continue;
        }
        if (terminal_col >= terminal_cols) {
            ++terminal_row;
            terminal_col = 0;
        }
        if (terminal_row >= terminal_rows) {
            terminal_scroll();
        }
        unsigned char c = (unsigned char)data[i];
        const uint8_t* glyph = font_8x16[c];
        terminal_draw_char(terminal_col++ * FONT_WIDTH, terminal_row * FONT_HEIGHT, glyph, fg, bg);
        if (show_cursor && terminal_col < terminal_cols && terminal_col >= 0 && terminal_row < terminal_rows && terminal_row >= 0)
            terminal_fill_rect(terminal_col * FONT_WIDTH, 
                        terminal_row * FONT_HEIGHT, 
                        FONT_WIDTH, 
                        FONT_HEIGHT, 
                        fg);
    }
}

void terminal_clear() {
    SpinlockGuard guard(tty_lock);
    uint32_t bg = terminal_reverse ? terminal_color : terminal_bg_color;
    terminal_fill_rect(0, 0, fb_width, fb_height, bg);
    terminal_row = 0;
    terminal_col = 0;
}

void terminal_set_read_wait_time(uint32_t ms) {
    read_wait_time = ms;
}

int terminal_read_char() {
    bool raw = (setting.c_lflag & ICANON) == 0;

    while (1) {
        if (process_list[cur_process_id]->signal) {
            return -1;
        }
        {
            SpinlockGuard ttyguard(tty_lock);
            if (kbd_buffer.head != kbd_buffer.tail) {
                break;
            }
            {
                SpinlockGuard guard(process_list_lock);
                process_list[cur_process_id]->state = process_state::WAITING;
                insert_into_waiting_queue(tty_wait_queue, process_list[cur_process_id]);
            }
        }
        if (raw && setting.c_cc[VMIN] == 0) {
            // VMIN=0: 超时等待，没数据就返回 -1
            timeout(&tty_wait_queue, read_wait_time);
            SpinlockGuard ttyguard(tty_lock);
            if (kbd_buffer.head == kbd_buffer.tail) {
                return -1;
            }
            break;
        } else {
            yield();
        }
    }
    SpinlockGuard guard(tty_lock);
    char c = rb_read();
    return c;
}

int terminal_read_char_for_peek() {
    SpinlockGuard ttyguard(tty_lock);
    if (kbd_buffer.head == kbd_buffer.tail) {
        return 0;
    }
    char c = rb_read();
    return c;
}

static int fb_write(const char* buffer, uint32_t offset, uint32_t size) {
    uint32_t fb_size = fb_pitch * fb_height;
    if (offset >= fb_size) return 0;
    uint32_t cpy_size = (fb_size - offset < size) ? fb_size - offset : size;
    memcpy((uint8_t*)fb_addr + offset, buffer, cpy_size);
    return cpy_size;
}

void init_fb_dev_file(mounting_point* mp) {
    static dev_operation fb_opr;
    fb_opr.read = nullptr;
    fb_opr.write = fb_write;
    register_in_devfs(mp, "fb0", &fb_opr);
}