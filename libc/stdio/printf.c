#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

static const char hex_digits_lower[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
static const char hex_digits_upper[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

static bool print(const char* data, size_t length) {
	const unsigned char* bytes = (const unsigned char*) data;
	for (size_t i = 0; i < length; i++)
		if (putchar(bytes[i]) == EOF)
			return false;
	return true;
}

void print_int(int num) {
    if (num < 0) {
        putchar('-');
        print_int(-num);
        return;
    }
    if (num >= 10) {
        print_int(num / 10);
        putchar('0' + (num % 10));
        return;
    }
    putchar('0' + num);
    return;
}

void print_uint64_hex(uint64_t num, bool islower) {
    if (num >= 16) {
        print_uint64_hex(num / 16, islower);
        putchar(islower ? hex_digits_lower[num % 16] : hex_digits_upper[num % 16]);
        return;
    }
    putchar(islower ? hex_digits_lower[num % 16] : hex_digits_upper[num % 16]);
    return;
}

void print_int_hex(uint32_t num, bool islower) {
    if (num >= 16) {
        print_int_hex(num / 16, islower);
        putchar(islower ? hex_digits_lower[num % 16] : hex_digits_upper[num % 16]);
        return;
    }
    putchar(islower ? hex_digits_lower[num % 16] : hex_digits_upper[num % 16]);
    return;
}

int printf(const char* restrict format, ...) {
    va_list args;
    va_start(args, format);
    const char* p = format;
    while (*p != '\0') {
        if (*p != '%') {
            putchar(*p);
            ++p;
            continue;
        }
        ++p;

        bool left_align = false;
        if (*p == '-') {
            left_align = true;
            ++p;
        }

        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            ++p;
        }

        switch (*p) {
            case 'd': {
                int num = va_arg(args, int);
                print_int(num);
                break;
            }
            case 's': {
                char* s = va_arg(args, char*);
                size_t len = strlen(s);
                if (left_align) {
                    print(s, len);
                    for (size_t i = len; i < (size_t)width; i++) putchar(' ');
                } else {
                    for (size_t i = len; i < (size_t)width; i++) putchar(' ');
                    print(s, len);
                }
                break;
            }
            case 'x': {
                putchar('0');
                putchar('x');
                uint32_t num = va_arg(args, uint32_t);
                print_int_hex(num, true);
                break;
            }
            case 'X': {
                putchar('0');
                putchar('x');
                uint32_t num = va_arg(args, uint32_t);
                print_int_hex(num, false);
                break;
            }
            case 'c': {
                int c = va_arg(args, int);
                putchar(c);
                break;
            }
            case 'l': {
                ++p;
                switch (*p) {
                    case 'u': { // todo: 不规范，但能用，先这么设置
                        uint64_t num = va_arg(args, uint64_t);
                        print_uint64_hex(num, true);
                        break;
                    }
                    default: {
                        putchar('%');
                        putchar('l');
                        putchar(*p);
                    }
                }
                break;
            }
            default: {
                putchar('%');
                putchar(*p);
            }
        }
        ++p;
    }
    va_end(args);
    return 0;
}