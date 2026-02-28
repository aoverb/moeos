#ifndef _FORMAT_H
#define _FORMAT_H
#include <string.h>
/*
 * format.h — Freestanding formatted output & string utilities for OS development
 *
 * No libc dependency. Provides:
 *   - snprintf / sprintf family (subset of standard format specifiers)
 *   - strcat / strncat
 *   - strlen
 *   - character/integer/hex/string to buffer helpers
 *
 * Supported format specifiers:
 *   %d / %i   — signed decimal integer
 *   %u        — unsigned decimal integer
 *   %x / %X   — unsigned hexadecimal (lower / upper)
 *   %o        — unsigned octal
 *   %b        — unsigned binary
 *   %s        — null-terminated string
 *   %c        — single character
 *   %p        — pointer (0x prefixed hex)
 *   %ld / %lu / %lx / %lX / %lo / %lb — long variants
 *   %lld / %llu / %llx / %llX / %llo / %llb — long long variants
 *   %%        — literal '%'
 *   Width & zero-padding supported: e.g. %08x, %4d
 *   '-' flag for left-justify: e.g. %-10s
 */

typedef unsigned long  size_t;
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

/* ========================================================================= */
/*  String utilities                                                         */
/* ========================================================================= */

/* strcat — 拼接 src 到 dst 末尾，返回 dst */
static inline char *strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

/* strncat — 最多拼接 n 个字符，始终以 '\0' 结尾 */
static inline char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (*d) d++;
    while (n-- && *src)
        *d++ = *src++;
    *d = '\0';
    return dst;
}

/* ========================================================================= */
/*  Number → string helpers                                                  */
/* ========================================================================= */

/*
 * _fmt_utoa_base — 将无符号整数转为指定进制的字符串写入 buf。
 * 返回写入的字符数（不含 '\0'）。buf 至少需要 65 字节（64-bit binary + NUL）。
 */
static inline int _fmt_utoa_base(char *buf, unsigned long long val,
                                 int base, int uppercase)
{
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;
    char tmp[65];
    int  i = 0, len;

    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val) {
            tmp[i++] = digits[val % base];
            val /= base;
        }
    }
    len = i;
    /* reverse into buf */
    for (int j = 0; j < len; j++)
        buf[j] = tmp[len - 1 - j];
    buf[len] = '\0';
    return len;
}

static inline int _fmt_itoa(char *buf, long long val)
{
    if (val < 0) {
        buf[0] = '-';
        return 1 + _fmt_utoa_base(buf + 1, (unsigned long long)(-val), 10, 0);
    }
    return _fmt_utoa_base(buf, (unsigned long long)val, 10, 0);
}

/* ========================================================================= */
/*  Core formatted output: vsnprintf                                         */
/* ========================================================================= */

static inline int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    size_t pos = 0;   /* 当前写入位置 */
    int    total = 0; /* 理论输出总长（即使被截断） */

    #define _FMT_PUT(ch) do {               \
        if (pos + 1 < size) buf[pos] = (ch);\
        pos++; total++;                      \
    } while (0)

    while (*fmt) {
        if (*fmt != '%') {
            _FMT_PUT(*fmt);
            fmt++;
            continue;
        }
        fmt++; /* skip '%' */

        /* --- flags --- */
        int  left_justify = 0;
        char pad_char     = ' ';

        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left_justify = 1;
            if (*fmt == '0' && !left_justify) pad_char = '0';
            fmt++;
        }

        /* --- width --- */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* --- length modifier --- */
        int length = 0; /* 0=int, 1=long, 2=long long */
        while (*fmt == 'l') { length++; fmt++; }

        /* --- specifier --- */
        char  numbuf[65];
        const char *s = numbuf;
        int   slen = 0;

        switch (*fmt) {
        case 'd': case 'i': {
            long long v;
            if      (length >= 2) v = va_arg(ap, long long);
            else if (length == 1) v = va_arg(ap, long);
            else                  v = va_arg(ap, int);
            slen = _fmt_itoa(numbuf, v);
            break;
        }
        case 'u': {
            unsigned long long v;
            if      (length >= 2) v = va_arg(ap, unsigned long long);
            else if (length == 1) v = va_arg(ap, unsigned long);
            else                  v = va_arg(ap, unsigned int);
            slen = _fmt_utoa_base(numbuf, v, 10, 0);
            break;
        }
        case 'x': case 'X': {
            unsigned long long v;
            if      (length >= 2) v = va_arg(ap, unsigned long long);
            else if (length == 1) v = va_arg(ap, unsigned long);
            else                  v = va_arg(ap, unsigned int);
            slen = _fmt_utoa_base(numbuf, v, 16, (*fmt == 'X'));
            break;
        }
        case 'o': {
            unsigned long long v;
            if      (length >= 2) v = va_arg(ap, unsigned long long);
            else if (length == 1) v = va_arg(ap, unsigned long);
            else                  v = va_arg(ap, unsigned int);
            slen = _fmt_utoa_base(numbuf, v, 8, 0);
            break;
        }
        case 'b': {
            unsigned long long v;
            if      (length >= 2) v = va_arg(ap, unsigned long long);
            else if (length == 1) v = va_arg(ap, unsigned long);
            else                  v = va_arg(ap, unsigned int);
            slen = _fmt_utoa_base(numbuf, v, 2, 0);
            break;
        }
        case 'p': {
            unsigned long long v = (unsigned long long)(unsigned long)va_arg(ap, void *);
            numbuf[0] = '0'; numbuf[1] = 'x';
            slen = 2 + _fmt_utoa_base(numbuf + 2, v, 16, 0);
            pad_char = '0';
            break;
        }
        case 's': {
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            slen = (int)strlen(s);
            break;
        }
        case 'c': {
            numbuf[0] = (char)va_arg(ap, int);
            numbuf[1] = '\0';
            slen = 1;
            break;
        }
        case '%':
            numbuf[0] = '%';
            numbuf[1] = '\0';
            slen = 1;
            break;
        default:
            /* 未知格式符：原样输出 */
            _FMT_PUT('%');
            _FMT_PUT(*fmt);
            fmt++;
            continue;
        }
        fmt++;

        /* --- padding & output --- */
        int pad = (width > slen) ? (width - slen) : 0;

        if (!left_justify) {
            /* 对于 '0' 填充的负数，先输出 '-' 再填 '0' */
            if (pad_char == '0' && slen > 0 && s[0] == '-') {
                _FMT_PUT('-');
                s++; slen--;
            }
            for (int i = 0; i < pad; i++) _FMT_PUT(pad_char);
        }
        for (int i = 0; i < slen; i++) _FMT_PUT(s[i]);
        if (left_justify) {
            for (int i = 0; i < pad; i++) _FMT_PUT(' ');
        }
    }

    /* NUL terminate */
    if (size > 0)
        buf[(pos < size) ? pos : size - 1] = '\0';

    #undef _FMT_PUT
    return total;
}

/* ========================================================================= */
/*  Public API                                                               */
/* ========================================================================= */

static inline int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

static inline int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return n;
}

/*
 * format_to — 格式化输出并通过回调函数逐字符发送
 *
 * 用法示例（配合串口/VGA 等输出）：
 *   void putchar_serial(char c) { outb(0x3F8, c); }
 *   format_to(putchar_serial, "Hello %s, val=0x%08x\n", name, val);
 */
typedef void (*putchar_fn)(char c);

static inline int vformat_to(putchar_fn put, const char *fmt, va_list ap)
{
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    int len = n < (int)sizeof(buf) - 1 ? n : (int)sizeof(buf) - 1;
    for (int i = 0; i < len; i++)
        put(buf[i]);
    return n;
}

static inline int format_to(putchar_fn put, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vformat_to(put, fmt, ap);
    va_end(ap);
    return n;
}

#endif /* _FORMAT_H */