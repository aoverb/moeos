#ifndef _FORMAT_H
#define _FORMAT_H
#include <string.h>
/*
 * format.h — Freestanding formatted I/O & string utilities for OS development
 *
 * No libc dependency. Provides:
 *   - snprintf / sprintf family (subset of standard format specifiers)
 *   - vsscanf / sscanf (subset of standard scan specifiers)
 *   - strcat / strncat
 *   - strlen
 *   - character/integer/hex/string to buffer helpers
 *
 * Supported format specifiers (output):
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
 *
 * Supported format specifiers (input / scanf):
 *   %d / %i   — signed decimal integer (%i also accepts 0x and 0 prefixes)
 *   %u        — unsigned decimal integer
 *   %x / %X   — unsigned hexadecimal
 *   %o        — unsigned octal
 *   %b        — unsigned binary
 *   %s        — whitespace-delimited string
 *   %c        — single character (does NOT skip whitespace)
 *   %p        — pointer (with optional 0x prefix)
 *   %n        — number of characters consumed so far
 *   %[...]    — scanset (e.g. %[abc], %[^/], %[0-9a-f])
 *   %%        — literal '%'
 *   Width limit supported: e.g. %4d, %10s
 *   '*' assignment-suppression: e.g. %*d
 *   Length modifiers: l, ll
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
/*  Internal helpers                                                         */
/* ========================================================================= */

static inline int _fmt_isspace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\f' || c == '\v';
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
    size_t pos = 0;
    int    total = 0;

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
        fmt++;

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
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) { left_justify = 1; width = -width; }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
        }

        /* --- precision --- */
        int has_prec  = 0;
        int precision = -1;
        if (*fmt == '.') {
            has_prec  = 1;
            precision = 0;
            fmt++;
            if (*fmt == '*') {
                precision = va_arg(ap, int);
                if (precision < 0) { has_prec = 0; precision = -1; }
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }
        }

        /* --- length modifier --- */
        int length = 0;
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
            if (has_prec) {
                int digits = (numbuf[0] == '-') ? slen - 1 : slen;
                if (precision > digits) {
                    char tmp[65];
                    int ti = 0;
                    if (numbuf[0] == '-') tmp[ti++] = '-';
                    for (int p = 0; p < precision - digits; p++) tmp[ti++] = '0';
                    int src = (numbuf[0] == '-') ? 1 : 0;
                    for (int p = src; p < slen; p++) tmp[ti++] = numbuf[p];
                    tmp[ti] = '\0';
                    for (int p = 0; p <= ti; p++) numbuf[p] = tmp[p];
                    slen = ti;
                }
                pad_char = ' ';
            }
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
            if (has_prec && precision < slen) slen = precision;
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
            _FMT_PUT('%');
            _FMT_PUT(*fmt);
            fmt++;
            continue;
        }
        fmt++;

        /* --- padding & output --- */
        int pad = (width > slen) ? (width - slen) : 0;

        if (!left_justify) {
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

    if (size > 0)
        buf[(pos < size) ? pos : size - 1] = '\0';

    #undef _FMT_PUT
    return total;
}
/* ========================================================================= */
/*  Core formatted input: vsscanf                                            */
/* ========================================================================= */

/*
 * _fmt_scan_uint — 从字符串 *src 中解析一个无符号整数（指定进制）。
 *
 * max_chars: 最多读取字符数 (0 = 不限制)
 * consumed: 输出实际消耗的字符数
 * 返回解析出的值。如果首字符就不是合法数字，*consumed = 0。
 */
static inline unsigned long long
_fmt_scan_uint(const char *src, int base, int max_chars, int *consumed)
{
    unsigned long long val = 0;
    int i = 0;

    while (src[i] && (max_chars == 0 || i < max_chars)) {
        char c = src[i];
        int  digit;

        if      (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else break;

        if (digit >= base) break;

        val = val * (unsigned long long)base + (unsigned long long)digit;
        i++;
    }
    *consumed = i;
    return val;
}

/*
 * _fmt_vsscanf_impl — 格式化输入的内部实现。
 *
 * safe = 0: 标准 vsscanf 行为
 * safe = 1: 安全模式 (vsscanf_s)
 *           %s, %c, %[...] 在 char* 参数后要求紧跟一个 size_t 参数，
 *           表示目标缓冲区大小。写入不会超过该大小（含 '\0'）。
 *
 * 支持的格式符：
 *   %d %i %u %x %X %o %b %s %c %p %n %[...] %%
 *   宽度限制：%4d, %10s 等
 *   赋值抑制：%*d 跳过不存储
 *   长度修饰：l, ll
 *
 * 返回成功匹配并赋值的项数（%*x 和 %n 不计入）。
 * 输入耗尽时返回 -1（EOF）。
 */
static inline int _fmt_vsscanf_impl(const char *input, const char *fmt,
                                     va_list ap, int safe)
{
    const char *inp = input;  /* 当前输入位置 */
    int matched = 0;          /* 成功赋值计数 */

    while (*fmt) {

        /* ---- 格式串中的空白：匹配输入中 0 或多个空白 ---- */
        if (_fmt_isspace(*fmt)) {
            while (_fmt_isspace(*fmt)) fmt++;
            while (_fmt_isspace(*inp)) inp++;
            continue;
        }

        /* ---- 非 '%' 的普通字符：必须精确匹配 ---- */
        if (*fmt != '%') {
            if (*inp != *fmt) return matched;
            inp++;
            fmt++;
            continue;
        }

        fmt++; /* skip '%' */

        /* --- %% 字面量 --- */
        if (*fmt == '%') {
            if (*inp != '%') return matched;
            inp++;
            fmt++;
            continue;
        }

        /* --- 赋值抑制 '*' --- */
        int suppress = 0;
        if (*fmt == '*') {
            suppress = 1;
            fmt++;
        }

        /* --- 宽度限制 --- */
        int max_width = 0; /* 0 = 不限 */
        while (*fmt >= '0' && *fmt <= '9') {
            max_width = max_width * 10 + (*fmt - '0');
            fmt++;
        }

        /* --- 长度修饰符 --- */
        int length = 0; /* 0=int, 1=long, 2=long long */
        while (*fmt == 'l') { length++; fmt++; }

        /* --- 说明符 --- */
        switch (*fmt) {

        /* ---- %d: 有符号十进制 ---- */
        case 'd': {
            /* 跳过前导空白 */
            while (_fmt_isspace(*inp)) inp++;
            if (!*inp) return matched ? matched : -1;

            int sign = 1;
            const char *start = inp;

            if (*inp == '-')      { sign = -1; inp++; }
            else if (*inp == '+') { inp++; }

            /* 计算数字可用宽度 */
            int digits_width = max_width ? max_width - (int)(inp - start) : 0;
            if (max_width && digits_width <= 0) return matched;

            int consumed = 0;
            unsigned long long uval = _fmt_scan_uint(inp, 10, digits_width, &consumed);
            if (consumed == 0) return matched; /* 没读到任何数字 */
            inp += consumed;

            if (!suppress) {
                long long val = (long long)uval * sign;
                if      (length >= 2) *va_arg(ap, long long *)      = val;
                else if (length == 1) *va_arg(ap, long *)           = (long)val;
                else                  *va_arg(ap, int *)             = (int)val;
                matched++;
            }
            break;
        }

        /* ---- %i: 自动检测进制 (0x=16, 0=8, 否则10) ---- */
        case 'i': {
            while (_fmt_isspace(*inp)) inp++;
            if (!*inp) return matched ? matched : -1;

            int sign = 1;
            const char *start = inp;

            if (*inp == '-')      { sign = -1; inp++; }
            else if (*inp == '+') { inp++; }

            int used_for_sign = (int)(inp - start);
            int remaining = max_width ? max_width - used_for_sign : 0;
            if (max_width && remaining <= 0) return matched;

            int base = 10;
            /* 检测 0x 或 0 前缀 */
            if (*inp == '0') {
                if ((inp[1] == 'x' || inp[1] == 'X') &&
                    (!max_width || remaining > 2)) {
                    base = 16;
                    inp += 2;
                    remaining = remaining > 2 ? remaining - 2 : 0;
                } else {
                    base = 8;
                }
            }

            int consumed = 0;
            unsigned long long uval = _fmt_scan_uint(inp, base, remaining, &consumed);
            /* 对于 0x 前缀：如果 0x 后没有数字，回退只匹配 "0" */
            if (consumed == 0 && base == 16) {
                inp -= 2; /* 回退 0x */
                uval = 0;
                consumed = 1; /* 匹配那个 '0' */
            }
            if (consumed == 0 && base != 16) return matched;
            inp += consumed;

            if (!suppress) {
                long long val = (long long)uval * sign;
                if      (length >= 2) *va_arg(ap, long long *)      = val;
                else if (length == 1) *va_arg(ap, long *)           = (long)val;
                else                  *va_arg(ap, int *)             = (int)val;
                matched++;
            }
            break;
        }

        /* ---- %u: 无符号十进制 ---- */
        case 'u': {
            while (_fmt_isspace(*inp)) inp++;
            if (!*inp) return matched ? matched : -1;

            int consumed = 0;
            unsigned long long val = _fmt_scan_uint(inp, 10, max_width, &consumed);
            if (consumed == 0) return matched;
            inp += consumed;

            if (!suppress) {
                if      (length >= 2) *va_arg(ap, unsigned long long *) = val;
                else if (length == 1) *va_arg(ap, unsigned long *)      = (unsigned long)val;
                else                  *va_arg(ap, unsigned int *)        = (unsigned int)val;
                matched++;
            }
            break;
        }

        /* ---- %x / %X: 十六进制（可选 0x 前缀） ---- */
        case 'x': case 'X': {
            while (_fmt_isspace(*inp)) inp++;
            if (!*inp) return matched ? matched : -1;

            int remaining = max_width;
            /* 跳过可选的 0x/0X 前缀 */
            if (inp[0] == '0' && (inp[1] == 'x' || inp[1] == 'X')) {
                if (!max_width || remaining > 2) {
                    inp += 2;
                    if (remaining) remaining -= 2;
                }
            }

            int consumed = 0;
            unsigned long long val = _fmt_scan_uint(inp, 16, remaining, &consumed);
            if (consumed == 0) return matched;
            inp += consumed;

            if (!suppress) {
                if      (length >= 2) *va_arg(ap, unsigned long long *) = val;
                else if (length == 1) *va_arg(ap, unsigned long *)      = (unsigned long)val;
                else                  *va_arg(ap, unsigned int *)        = (unsigned int)val;
                matched++;
            }
            break;
        }

        /* ---- %o: 八进制 ---- */
        case 'o': {
            while (_fmt_isspace(*inp)) inp++;
            if (!*inp) return matched ? matched : -1;

            int consumed = 0;
            unsigned long long val = _fmt_scan_uint(inp, 8, max_width, &consumed);
            if (consumed == 0) return matched;
            inp += consumed;

            if (!suppress) {
                if      (length >= 2) *va_arg(ap, unsigned long long *) = val;
                else if (length == 1) *va_arg(ap, unsigned long *)      = (unsigned long)val;
                else                  *va_arg(ap, unsigned int *)        = (unsigned int)val;
                matched++;
            }
            break;
        }

        /* ---- %b: 二进制（非标准，与你的 printf %b 配对） ---- */
        case 'b': {
            while (_fmt_isspace(*inp)) inp++;
            if (!*inp) return matched ? matched : -1;

            int consumed = 0;
            unsigned long long val = _fmt_scan_uint(inp, 2, max_width, &consumed);
            if (consumed == 0) return matched;
            inp += consumed;

            if (!suppress) {
                if      (length >= 2) *va_arg(ap, unsigned long long *) = val;
                else if (length == 1) *va_arg(ap, unsigned long *)      = (unsigned long)val;
                else                  *va_arg(ap, unsigned int *)        = (unsigned int)val;
                matched++;
            }
            break;
        }

        /* ---- %p: 指针（与 printf 的 0xHHHH 格式配对） ---- */
        case 'p': {
            while (_fmt_isspace(*inp)) inp++;
            if (!*inp) return matched ? matched : -1;

            /* 跳过可选 0x 前缀 */
            if (inp[0] == '0' && (inp[1] == 'x' || inp[1] == 'X'))
                inp += 2;

            int consumed = 0;
            unsigned long long val = _fmt_scan_uint(inp, 16, 0, &consumed);
            if (consumed == 0) return matched;
            inp += consumed;

            if (!suppress) {
                *va_arg(ap, void **) = (void *)(unsigned long)val;
                matched++;
            }
            break;
        }

        /* ---- %s: 非空白字符串 ---- */
        case 's': {
            while (_fmt_isspace(*inp)) inp++;
            if (!*inp) return matched ? matched : -1;

            if (!suppress) {
                char  *dst   = va_arg(ap, char *);
                size_t bufsz = safe ? va_arg(ap, size_t) : 0;
                /* safe 模式下 bufsz==0 视为错误，写入空串后返回 */
                if (safe && bufsz == 0) return matched;
                size_t limit = (size_t)-1; /* 默认无限 */
                if (safe)      limit = bufsz - 1;
                if (max_width) limit = (safe && (size_t)max_width < limit)
                                       ? (size_t)max_width : (max_width && !safe)
                                       ? (size_t)max_width : limit;
                int count = 0;
                while (*inp && !_fmt_isspace(*inp) && (size_t)count < limit) {
                    *dst++ = *inp++;
                    count++;
                }
                *dst = '\0';
                /* 非 safe 模式下，如果有 max_width，还需跳过剩余字符 */
                if (!safe && max_width) {
                    /* 已经写了 count 个, 不需要额外跳过 */
                } else if (safe && max_width && (size_t)max_width > limit) {
                    /* safe 模式下缓冲区满了但 max_width 还没到，跳过剩余 */
                    while (*inp && !_fmt_isspace(*inp) && count < max_width) {
                        inp++;
                        count++;
                    }
                }
                matched++;
            } else {
                int count = 0;
                while (*inp && !_fmt_isspace(*inp) &&
                       (max_width == 0 || count < max_width)) {
                    inp++;
                    count++;
                }
            }
            break;
        }

        /* ---- %c: 单个/多个字符（不跳过空白） ---- */
        case 'c': {
            int want = max_width ? max_width : 1;
            if (!*inp) return matched ? matched : -1;

            if (!suppress) {
                char  *dst   = va_arg(ap, char *);
                size_t bufsz = safe ? va_arg(ap, size_t) : 0;
                if (safe && bufsz == 0) return matched;
                int limit = safe ? (int)bufsz : want;
                if (want < limit) limit = want;
                int i;
                for (i = 0; i < limit && *inp; i++)
                    *dst++ = *inp++;
                /* safe 模式下如果还没读够 want 个，继续消耗输入但不写入 */
                for (; i < want && *inp; i++)
                    inp++;
                matched++;
            } else {
                for (int i = 0; i < want && *inp; i++)
                    inp++;
            }
            break;
        }

        /* ---- %n: 已消耗字符数（不计入 matched） ---- */
        case 'n': {
            if (!suppress) {
                if      (length >= 2) *va_arg(ap, long long *) = (long long)(inp - input);
                else if (length == 1) *va_arg(ap, long *)      = (long)(inp - input);
                else                  *va_arg(ap, int *)        = (int)(inp - input);
            }
            break;
        }

        /* ---- %[...]: scanset ---- */
        case '[': {
            /* 构建一个 256-bit 查找表，标记哪些字符在集合中 */
            unsigned char set[256 / 8]; /* 32 bytes, 每 bit 对应一个 char */
            for (int i = 0; i < (int)sizeof(set); i++) set[i] = 0;

            int negate = 0;
            fmt++; /* skip '[' */

            if (*fmt == '^') {
                negate = 1;
                fmt++;
            }

            /* ']' 如果紧跟在 '[' 或 '[^' 后面，视为集合中的普通字符 */
            if (*fmt == ']') {
                set[(unsigned char)']' / 8] |= (1 << ((unsigned char)']' % 8));
                fmt++;
            }

            /* 解析集合内容直到 ']' 或字符串结束 */
            while (*fmt && *fmt != ']') {
                unsigned char c = (unsigned char)*fmt;

                /* 处理范围 a-z */
                if (fmt[1] == '-' && fmt[2] && fmt[2] != ']') {
                    unsigned char lo = c;
                    unsigned char hi = (unsigned char)fmt[2];
                    if (lo > hi) { unsigned char t = lo; lo = hi; hi = t; }
                    for (unsigned int ch = lo; ch <= hi; ch++)
                        set[ch / 8] |= (1 << (ch % 8));
                    fmt += 3;
                } else {
                    set[c / 8] |= (1 << (c % 8));
                    fmt++;
                }
            }
            /* fmt 现在指向 ']'（或 '\0'）。
             * 循环末尾的 fmt++ 会跳过 ']'。 */

            #define _SCANSET_HAS(ch) \
                (!!( set[(unsigned char)(ch) / 8] & (1 << ((unsigned char)(ch) % 8)) ))

            if (!*inp) { return matched ? matched : -1; }

            if (!suppress) {
                char  *dst   = va_arg(ap, char *);
                size_t bufsz = safe ? va_arg(ap, size_t) : 0;
                if (safe && bufsz == 0) return matched;
                size_t limit = (size_t)-1;
                if (safe)      limit = bufsz - 1;
                if (max_width && (size_t)max_width < limit) limit = (size_t)max_width;

                int count = 0;
                while (*inp && (size_t)count < limit) {
                    int in_set = _SCANSET_HAS(*inp);
                    if (negate ? in_set : !in_set) break;
                    *dst++ = *inp++;
                    count++;
                }
                /* safe 模式下缓冲区满了，继续消耗匹配的输入 */
                if (safe && max_width) {
                    while (*inp && count < max_width) {
                        int in_set = _SCANSET_HAS(*inp);
                        if (negate ? in_set : !in_set) break;
                        inp++;
                        count++;
                    }
                } else if (safe) {
                    while (*inp) {
                        int in_set = _SCANSET_HAS(*inp);
                        if (negate ? in_set : !in_set) break;
                        inp++;
                        count++;
                    }
                }
                if (count == 0) return matched;
                *dst = '\0';
                matched++;
            } else {
                int count = 0;
                while (*inp && (max_width == 0 || count < max_width)) {
                    int in_set = _SCANSET_HAS(*inp);
                    if (negate ? in_set : !in_set) break;
                    inp++;
                    count++;
                }
                if (count == 0) return matched;
            }

            #undef _SCANSET_HAS
            break;
        }

        default:
            /* 未知格式符，停止解析 */
            return matched;
        }

        fmt++;
    }

    /* 如果一项都没匹配且输入为空，返回 EOF (-1) */
    if (matched == 0 && !*input)
        return -1;

    return matched;
}

/* vsscanf — 标准版 */
static inline int vsscanf(const char *input, const char *fmt, va_list ap)
{
    return _fmt_vsscanf_impl(input, fmt, ap, 0);
}

/*
 * vsscanf_s — 安全版：%s, %c, %[...] 在 char* 后需要紧跟 size_t 参数
 *
 * 用法：
 *   char buf[16];
 *   vsscanf_s(input, "%s", ap);   // ap 中依次取 buf, (size_t)sizeof(buf)
 */
static inline int vsscanf_s(const char *input, const char *fmt, va_list ap)
{
    return _fmt_vsscanf_impl(input, fmt, ap, 1);
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

static inline int sscanf(const char *input, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsscanf(input, fmt, ap);
    va_end(ap);
    return n;
}

/*
 * sscanf_s — 安全版 sscanf
 *
 * %s, %c, %[...] 的每个 char* 参数后必须紧跟一个 size_t 表示缓冲区大小。
 * 数值类型（%d, %u, %x 等）用法与普通 sscanf 完全相同。
 *
 * 示例：
 *   char name[8], city[16];
 *   int age;
 *   sscanf_s("Alice 30 Tokyo", "%s %d %s",
 *            name, (size_t)sizeof(name),     // %s → char* + size_t
 *            &age,                            // %d → int*（无需 size_t）
 *            city, (size_t)sizeof(city));     // %s → char* + size_t
 *
 *   // 即使输入超长也不会溢出：
 *   char tiny[4];
 *   sscanf_s("hello", "%s", tiny, (size_t)sizeof(tiny));
 *   // tiny == "hel"  （3 字符 + '\0'，安全截断）
 */
static inline int sscanf_s(const char *input, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsscanf_s(input, fmt, ap);
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