// doomgeneric_lolios.c — DOOM platform layer for lolios

#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

// ----- Framebuffer -----

static int fb_fd = -1;
static uint32_t screen_width = 0;
static uint32_t screen_height = 0;
static uint32_t screen_pitch = 0;

#define FB_GET_INFO 0

struct fb_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
};

// ----- Keyboard -----

static int kbd_fd = -1;

struct key_event {
    uint8_t scancode;
    uint8_t pressed;
    uint8_t is_e0;
    uint8_t _pad;
};

#define KEYQUEUE_SIZE 32
static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static unsigned char convertToDoomKey(uint8_t scancode, uint8_t is_e0)
{
    if (is_e0) {
        switch (scancode) {
        case 0x48: return KEY_UPARROW;
        case 0x50: return KEY_DOWNARROW;
        case 0x4B: return KEY_LEFTARROW;
        case 0x4D: return KEY_RIGHTARROW;
        default:   return 0;
        }
    }

    switch (scancode) {
    case 0x1C: return KEY_ENTER;
    case 0x01: return KEY_ESCAPE;
    case 0x39: return KEY_USE;         // space
    case 0x1D: return KEY_FIRE;        // left ctrl
    case 0x2A: return KEY_RSHIFT;      // left shift
    case 0x36: return KEY_RSHIFT;      // right shift
    case 0x38: return KEY_LALT;        // alt (strafe)
    case 0x0F: return KEY_TAB;         // tab (map)
    case 0x15: return 'y';             // for yes prompts
    // number keys 1-7 for weapon switching
    case 0x02: return '1';
    case 0x03: return '2';
    case 0x04: return '3';
    case 0x05: return '4';
    case 0x06: return '5';
    case 0x07: return '6';
    case 0x08: return '7';
    // WASD alternative
    case 0x11: return KEY_UPARROW;     // W
    case 0x1F: return KEY_DOWNARROW;   // S
    case 0x1E: return KEY_LEFTARROW;   // A
    case 0x20: return KEY_RIGHTARROW;  // D
    default:   return 0;
    }
}

static void addKeyToQueue(int pressed, unsigned char key)
{
    if (key == 0) return;
    unsigned short keyData = (pressed << 8) | key;
    s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
    s_KeyQueueWriteIndex = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
}

static void handleKeyInput(void)
{
    if (kbd_fd < 0) return;

    struct key_event ev;
    while (read(kbd_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        unsigned char key = convertToDoomKey(ev.scancode, ev.is_e0);
        addKeyToQueue(ev.pressed, key);
    }
}

// ----- DG interface -----

void DG_Init(void)
{
    // Open framebuffer
    fb_fd = open("/dev/fb0", 1); // O_WRONLY
    if (fb_fd >= 0) {
        struct fb_info info;
        if (ioctl(fb_fd, FB_GET_INFO, &info) == 0) {
            screen_width  = info.width;
            screen_height = info.height;
            screen_pitch  = info.pitch;
            printf("FB: %dx%d pitch=%d bpp=%d\n",
                   info.width, info.height, info.pitch, info.bpp);
        }
    } else {
        printf("Failed to open /dev/fb0\n");
    }

    // Open keyboard
    kbd_fd = open("/dev/kbd", 0); // O_RDONLY
    if (kbd_fd < 0) {
        printf("Failed to open /dev/kbd\n");
    }
}

void DG_DrawFrame(void)
{
    if (fb_fd < 0) return;

    lseek(fb_fd, 0, 0); // SEEK_SET

    // DG_ScreenBuffer is 320x200 XRGB8888
    // Write line by line, seeking to correct pitch offset
    for (int y = 0; y < DOOMGENERIC_RESY; ++y) {
        lseek(fb_fd, y * screen_pitch, 0);
        write(fb_fd,
              (const char*)(DG_ScreenBuffer + y * DOOMGENERIC_RESX),
              DOOMGENERIC_RESX * 4);
    }

    // Poll keyboard while we're at it
    handleKeyInput();
}

void DG_SleepMs(uint32_t ms)
{
    uint32_t target = time(0) * 10 + ms;
    while (time(0) * 10 < target) {
        yield();
    }
}

uint32_t DG_GetTicksMs(void)
{
    return time(0) * 10;
}

int DG_GetKey(int* pressed, unsigned char* doomKey)
{
    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex) {
        return 0;
    }

    unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;

    *pressed = keyData >> 8;
    *doomKey = keyData & 0xFF;

    return 1;
}

void DG_SetWindowTitle(const char* title)
{
    (void)title;
}

int main(int argc, char** argv)
{
    doomgeneric_Create(argc, argv);

    while (1) {
        doomgeneric_Tick();
    }

    return 0;
}