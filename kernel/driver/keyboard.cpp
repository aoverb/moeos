#include <driver/keyboard.h>
#include <stdio.h>
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

bool is_shift_pressed = false;

struct round_buffer {
    volatile char buffer[256];
    volatile uint8_t head = 0;
    volatile uint8_t tail = 0;
} kbd_buffer;

void rb_flush() {
    asm volatile("cli");
    kbd_buffer.head = kbd_buffer.tail = 0;
    asm volatile ("sti");
}

char rb_read(round_buffer* buf) {
    asm volatile ("cli");
    if (buf->head == buf->tail) {
        asm volatile ("sti");
        return -1;
    }
    char re = buf->buffer[buf->head++];
    asm volatile ("sti");
    return re;
    
}

void rb_write(round_buffer* buf, char data) {
    asm volatile ("cli");
    buf->buffer[(buf->tail)++] = data;
    if (buf->head == buf->tail) {
        ++(buf->head);
    }
    asm volatile ("sti");
}

void keyboard_interrupt_handler(registers* /* regs */) {
    uint8_t scancode = hal_inb(0x60);
    if (scancode & KEY_RELEASED_MASK) {
        if ((scancode ^ KEY_RELEASED_MASK) == 0x2A || (scancode ^ KEY_RELEASED_MASK) == 0x36) {
            is_shift_pressed = false;
        }
        return;
    }
    if (scancode == 0x2A || scancode == 0x36) {
        is_shift_pressed = true;
        return;
    }
    rb_write(&kbd_buffer, scancode_to_ascii_table[scancode][is_shift_pressed]);
    return;
}

void keyboard_init() {
    printf("Keyboard initializing...");
    rb_flush();
    register_interrupt_handler(33, keyboard_interrupt_handler);
    while (hal_inb(0x64) & 0x01) {
        hal_inb(0x60); // 读走数据，但不做任何处理，直接丢弃
    }

    hal_enable_irq(1);
    printf("OK\n");
}

void keyboard_flush() {
    rb_flush();
}

char keyboard_getchar() {
    return rb_read(&kbd_buffer);
}

bool keyboard_haschar() {
    return kbd_buffer.head != kbd_buffer.tail;
}