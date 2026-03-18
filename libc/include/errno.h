#ifndef _TERMIOS_H
#define _TERMIOS_H

#include <stdint.h>

typedef uint32_t tcflag_t;
typedef uint8_t  cc_t;
typedef uint32_t speed_t;

/* c_cc 数组大小与索引 */
#define NCCS    16
#define VMIN     0
#define VTIME    1
#define VINTR    2
#define VEOF     3
#define VERASE   4

/* c_iflag 位 */
#define BRKINT   0x0001
#define ICRNL    0x0002   /* 将输入 CR 映射为 NL */
#define INPCK    0x0004
#define ISTRIP   0x0008
#define IXON     0x0010   /* 启用 XON/XOFF 流控 */

/* c_oflag 位 */
#define OPOST    0x0001   /* 输出后处理 */

/* c_cflag 位 */
#define CS8      0x0010   /* 8-bit 字符 */
#define CSIZE    0x0030   /* 字符大小掩码 */

/* c_lflag 位 */
#define ECHO     0x0001   /* 回显 */
#define ICANON   0x0002   /* 规范模式（行缓冲） */
#define ISIG     0x0004   /* 启用信号（Ctrl+C 等） */
#define IEXTEN   0x0008   /* 扩展输入处理 */

/* tcsetattr 的 optional_actions */
#define TCSANOW     0     /* 立即生效 */
#define TCSADRAIN   1     /* 等输出完成后生效 */
#define TCSAFLUSH   2     /* 等输出完成 + 丢弃未读输入 */

struct termios {
    tcflag_t c_iflag;     /* 输入模式 */
    tcflag_t c_oflag;     /* 输出模式 */
    tcflag_t c_cflag;     /* 控制模式 */
    tcflag_t c_lflag;     /* 本地模式 */
    cc_t     c_cc[NCCS];  /* 控制字符 */
};

#ifdef __cplusplus
extern "C" {
#endif

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);

#ifdef __cplusplus
}
#endif

#endif /* _TERMIOS_H */