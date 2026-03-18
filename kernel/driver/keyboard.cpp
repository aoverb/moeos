#include <driver/keyboard.h>
#include <stdio.h>
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

/* 发送 VT100 转义序列：\x1b [ <suffix> */
static void send_escape_seq(const char* seq) {
    while (*seq) {
        terminal_input(*seq++);
    }
}

/* 处理 0xE0 前缀后的第二个扫描码 */
static void handle_e0_scancode(uint8_t scancode) {
    if (scancode & KEY_RELEASED_MASK) {
        return;
    }

    switch (scancode) {
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

    if (scancode & KEY_RELEASED_MASK) {
        uint8_t released = scancode ^ KEY_RELEASED_MASK;
        if (released == 0x2A || released == 0x36)
            is_shift_pressed = false;
        if (released == 0x1D)
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

void keyboard_init() {
    printf("Keyboard initializing...");
    register_interrupt_handler(33, keyboard_interrupt_handler);
    while (hal_inb(0x64) & 0x01) {
        hal_inb(0x60); // 读走数据，但不做任何处理，直接丢弃
    }

    hal_enable_irq(1);
    printf("OK\n");
}
