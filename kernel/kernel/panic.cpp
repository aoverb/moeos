#include <kernel/panic.h>
#include <kernel/tty.h>
#include <string.h>
#include <qrcodegen.h>
#include "mascot_data.h"

extern uint32_t fb_width;   // 屏幕宽度（像素）
extern uint32_t fb_height;  // 屏幕高度（像素）

// 可爱圆润风 5x5 字模 (bit4~bit0)
static const uint8_t cute_glyphs[6][5] = {
    {0x1F,0x11,0x1F,0x10,0x10}, // P  ██████ → ██  █ → ██████ → █     → █
    {0x0E,0x11,0x1F,0x11,0x11}, // A   ███  → █   █ → ██████ → █   █ → █   █
    {0x11,0x19,0x15,0x13,0x11}, // N  █   █ → ██  █ → █ █ █ → █  ██ → █   █
    {0x0E,0x04,0x04,0x04,0x0E}, // I   ███  →   █   →   █   →   █   →  ███
    {0x0E,0x11,0x10,0x11,0x0E}, // C   ███  → █   █ → █     → █   █ →  ███
    {0x04,0x04,0x04,0x00,0x04}, // !    █   →   █   →   █   →       →   █
};

void draw_mascot(int cx, int cy, int scale) {
    int ox = cx - (32 * scale) / 2;
    int oy = cy - (32 * scale) / 2;

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            uint32_t color = mascot_32x32[y][x];
            if (color == 0x0039C5BB) continue;
            terminal_fill_rect(ox + x * scale, oy + y * scale,
                              scale, scale, color);
        }
    }
}

void draw_cute_panic(int cx, int cy, int scale) {
    int total_w = 35 * scale;
    int total_h = 5 * scale;
    int ox = cx - total_w / 2;
    int oy = cy - total_h / 2;

    int shd = scale / 3;
    if (shd < 1) shd = 1;

    for (int i = 0; i < 6; i++) {
        int gx = ox + i * 6 * scale;

        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 5; c++)
                if (cute_glyphs[i][r] & (0x10 >> c))
                    terminal_fill_rect(gx + c*scale + shd, oy + r*scale + shd,
                                       scale, scale, 0x00207A73);

        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 5; c++)
                if (cute_glyphs[i][r] & (0x10 >> c))
                    terminal_fill_rect(gx + c*scale, oy + r*scale,
                                       scale, scale, 0x00FFFFFF);
    }
}

void make_qr(const char* text) {
    terminal_fill_rect(0, 0, fb_width, fb_height, 0x0039C5BB);
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(20)];
    uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(20)];

    bool ok = qrcodegen_encodeText(
        text, tmp, qr,
        qrcodegen_Ecc_LOW,
        qrcodegen_VERSION_MIN, 20,
        qrcodegen_Mask_AUTO, true
    );

    if (!ok) return;
    int size = qrcodegen_getSize(qr);

    int left_w = fb_width * 2018 / 2618;

    int avail = fb_height * 70 / 100;
    int ratio = avail / size;
    if (ratio < 1) ratio = 1;
    int qr_size = size * ratio;

    int qr_x = (left_w - qr_size) / 2;
    int qr_y = (fb_height - qr_size) / 2;

    int quiet = ratio * 2;
    terminal_fill_rect(
        qr_x - quiet, qr_y - quiet,
        qr_size + quiet * 2, qr_size + quiet * 2,
        0x00FFFFFF
    );

    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            terminal_fill_rect(qr_x + x * ratio, qr_y + y * ratio,
                            ratio, ratio,
                            qrcodegen_getModule(qr, x, y) ? 0x0039C5BB : 0x00FFFFFF);

    int qr_right = qr_x + qr_size + quiet;
    int right_cx = qr_right + (fb_width - qr_right) / 2;
    int screen_cy = fb_height / 2;

    int right_w = fb_width - qr_right;
    int mascot_scale = right_w * 60 / 100 / 32;
    if (mascot_scale < 2) mascot_scale = 2;
    int mascot_h = 32 * mascot_scale;

    int gap = mascot_h / 6;
    int text_scale = mascot_scale / 3;
    if (text_scale < 2) text_scale = 2;
    int text_h = 5 * text_scale;

    int content_h = mascot_h + gap + text_h;
    int top_y = screen_cy - content_h / 2;

    draw_mascot(right_cx, top_y + mascot_h / 2, mascot_scale);
    draw_cute_panic(right_cx, top_y + mascot_h + gap + text_h / 2, text_scale);
}

void panic(const char* str) {
    __asm__ volatile("cli");
    make_qr(str);
    while(1) __asm__ volatile("hlt");
}