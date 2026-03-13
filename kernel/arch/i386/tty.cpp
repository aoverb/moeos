/* kernel/arch/i386/tty.cpp */
#include <kernel/tty.h>
#include <kernel/font.h>
#include <kernel/mm.hpp>
#include <kernel/process.hpp>
#include <kernel/signal.h>
#include <kernel/schedule.hpp>
#include <string.h>
#include <boot/multiboot.h>
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
static process_queue tty_wait_queue = nullptr;

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
    if (c == 0x03) {
        terminal_write("^C\n", 3);
        if (foreground_pid == 0) return;
        send_signal(foreground_pid, SIGNAL::SIGINT);
        {
            SpinlockGuard ttyguard(tty_lock);
            SpinlockGuard guard(process_list_lock);
            if (process_list[foreground_pid]->inwait_queue &&
               *(process_list[foreground_pid]->inwait_queue)) {
                remove_from_waiting_queue(*(process_list[foreground_pid]->inwait_queue), foreground_pid);
        }
        process_list[foreground_pid]->state = process_state::READY;
        // 强制把进程的左右清空，反正已经remove过了
        process_list[foreground_pid]->prev = process_list[foreground_pid]->next = nullptr;
        insert_into_scheduling_queue(foreground_pid);

        }
        return;
    }
    {
        SpinlockGuard ttyguard(tty_lock);
        rb_write(c);
    }
}

void terminal_setforeground(pid_t pid) {
    SpinlockGuard guard(tty_lock);
    foreground_pid = pid;
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
}

void terminal_setcolor(uint32_t color) {
    terminal_color = color;
}

void terminal_putpixel(int x, int y, uint32_t color) {
    uint32_t offset = y * (fb_pitch / 4) + x;
    fb_addr[offset] = color;
}


void terminal_draw_char(int x, int y, const uint8_t* font_char, uint32_t color) {
    // 控制台绘制字体的逻辑不应该依赖于字体的具体实现！需要重构。但是现在只是用来简单输出字符，刚刚好够用。
    for (int row = 0; row < FONT_HEIGHT; row++) {
        for (int col = 0; col < FONT_WIDTH; col++) {
            if ((font_char[row] & (0x80 >> col))) {
                terminal_putpixel(x + col, y + row, color);
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
    terminal_fill_rect(0, (terminal_rows - 1) * FONT_HEIGHT, 
                      fb_width, FONT_HEIGHT, 0x00000000);
    
    terminal_row = terminal_rows - 1;
}

void terminal_write(const char* data, size_t size) {
    SpinlockGuard guard(tty_lock);
    for (size_t i = 0; i < size; i++) {
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
                              0x00000000);
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
        terminal_draw_char(terminal_col++ * FONT_WIDTH, terminal_row * FONT_HEIGHT, glyph, terminal_color);
    }
}

void terminal_clear() {
    SpinlockGuard guard(tty_lock);
    terminal_fill_rect(0, 0, fb_width, fb_height, 0x00000000);
    terminal_row = 0;
    terminal_col = 0;
}

int terminal_read_char() {
    while (1) {
        if (process_list[cur_process_id]->signal) {
            return -1;
        }
        {
            SpinlockGuard ttyguard(tty_lock);
            if (kbd_buffer.head != kbd_buffer.tail) {
                break;
            }
        }
        asm volatile ("pause");
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
