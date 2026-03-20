#include <driver/keyboard.h>
#include <driver/devfs.hpp>
#include <string.h>
#include <kernel/tty.h>
#include <kernel/hal.h>
#include <kernel/isr.h>

#define KEY_RELEASED_MASK 0x80

/* 定义一个足够大的数组覆盖到 0x66 (Backspace) */
const char scancode_to_ascii_table[128][2] = {
    /* 0x00 - 0x07 */ {0,0}, {27,27}, {'1','!'}, {'2','@'}, {'3','#'}, {'4','$'}, {'5','%'}, {'6','^'},
    /* 0x08 - 0x0F */ {'7','&'}, {'8','*'}, {'9','('}, {'0',')'}, {'-','_'}, {'=','+'}, {'\b','\b'}, {'\t','\t'},
    /* 0x10 - 0x17 */ {'q','Q'}, {'w','W'}, {'e','E'}, {'r','R'}, {'t','T'}, {'y','Y'}, {'u','U'}, {'i','I'},
    /* 0x18 - 0x1F */ {'o','O'}, {'p','P'}, {'[','{'}, {']','}'}, {'\n','\n'}, {0,0}, {'a','A'}, {'s','S'},
    /* 0x20 - 0x27 */ {'d','D'}, {'f','F'}, {'g','G'}, {'h','H'}, {'j','J'}, {'k','K'}, {'l','L'}, {';',':'},
    /* 0x28 - 0x2F */ {'\'','\"'}, {'`','~'}, {0,0}, {'\\','|'}, {'z','Z'}, {'x','X'}, {'c','C'}, {'v','V'},
    /* 0x30 - 0x37 */ {'b','B'}, {'n','N'}, {'m','M'}, {',','<'}, {'.','>'}, {'/','?'}, {0,0}, {'*', '*'},
    /* 0x38 - 0x3F */ {0,0}, {' ',' '}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}
};

static bool is_shift_pressed = false;
static bool is_ctrl_pressed = false;
static bool e0_prefix = false;

// ===== /dev/kbd 事件队列 =====

struct key_event {
    uint8_t scancode;
    uint8_t pressed;   // 1 = pressed, 0 = released
    uint8_t is_e0;     // 1 = 扩展键 (方向键等)
    uint8_t _pad;
};

#define KBD_BUF_SIZE 128
static key_event kbd_events[KBD_BUF_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;

static void kbd_push_event(uint8_t scancode, uint8_t pressed, uint8_t is_e0) {
    uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next == kbd_tail) return;  // 满了丢弃
    kbd_events[kbd_head] = { scancode, pressed, is_e0, 0 };
    kbd_head = next;
}

static int kbd_read(char* buffer, uint32_t /* offset */, uint32_t size) {
    uint32_t bytes_read = 0;
    while (bytes_read + sizeof(key_event) <= size && kbd_tail != kbd_head) {
        memcpy(buffer + bytes_read, &kbd_events[kbd_tail], sizeof(key_event));
        kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
        bytes_read += sizeof(key_event);
    }
    return bytes_read;
}

static int kbd_peek() {
    return (kbd_head != kbd_tail) ? 1 : 0;
}

void init_kbd_dev_file(mounting_point* mp) {
    static dev_operation kbd_opr;
    kbd_opr.read    = kbd_read;
    kbd_opr.write   = nullptr;
    kbd_opr.peek    = kbd_peek;
    kbd_opr.set_poll = nullptr;
    kbd_opr.ioctl   = nullptr;
    register_in_devfs(mp, "kbd", &kbd_opr);
}

/* 发送 VT100 转义序列：\x1b [ <suffix> */
static void send_escape_seq(const char* seq) {
    while (*seq) {
        terminal_input(*seq++);
    }
}

/* 处理 0xE0 前缀后的第二个扫描码 */
static void handle_e0_scancode(uint8_t scancode) {
    uint8_t pressed = !(scancode & KEY_RELEASED_MASK);
    uint8_t code = scancode & 0x7F;

    // 推送到 /dev/kbd（press 和 release 都推）
    kbd_push_event(code, pressed, 1);

    if (!pressed) return;

    switch (code) {
    case 0x48: send_escape_seq("\x1b[A");   break;  /* 上 */
    case 0x50: send_escape_seq("\x1b[B");   break;  /* 下 */
    case 0x4D: send_escape_seq("\x1b[C");   break;  /* 右 */
    case 0x4B: send_escape_seq("\x1b[D");   break;  /* 左 */
    case 0x47: send_escape_seq("\x1b[H");   break;  /* Home */
    case 0x4F: send_escape_seq("\x1b[F");   break;  /* End */
    case 0x53: send_escape_seq("\x1b[3~");  break;  /* Delete */
    case 0x52: send_escape_seq("\x1b[2~");  break;  /* Insert */
    case 0x49: send_escape_seq("\x1b[5~");  break;  /* Page Up */
    case 0x51: send_escape_seq("\x1b[6~");  break;  /* Page Down */
    default:
        break;
    }
}

void keyboard_interrupt_handler(registers* /* regs */) {
    uint8_t scancode = hal_inb(0x60);

    if (scancode == 0xE0) {
        e0_prefix = true;
        return;
    }

    if (e0_prefix) {
        e0_prefix = false;
        handle_e0_scancode(scancode);
        return;
    }

    uint8_t pressed = !(scancode & KEY_RELEASED_MASK);
    uint8_t code = scancode & 0x7F;

    kbd_push_event(code, pressed, 0);

    if (!pressed) {
        if (code == 0x2A || code == 0x36)
            is_shift_pressed = false;
        if (code == 0x1D)
            is_ctrl_pressed = false;
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        is_shift_pressed = true;
        return;
    }
    if (scancode == 0x1D) {
        is_ctrl_pressed = true;
        return;
    }

    char c = scancode_to_ascii_table[scancode][is_shift_pressed];
    if (c && is_ctrl_pressed) {
        c = c & 0x1F;
    }
    if (c) {
        terminal_input(c);
    }
}

extern "C" int printf(const char* fmt, ...);
void keyboard_init() {
    printf("Keyboard initializing...");
    register_interrupt_handler(33, keyboard_interrupt_handler);
    while (hal_inb(0x64) & 0x01) {
        hal_inb(0x60); // 读走数据，但不做任何处理，直接丢弃
    }

    hal_enable_irq(1);
    printf("OK\n");
}
