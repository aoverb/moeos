#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include <stdint.h>

/* ioctl 请求码 */
#define TCGETS   0x5401
#define TCSETS   0x5402
#define TCSETSW  0x5403
#define TCSETSF  0x5404
#define TIOCGWINSZ   0x5413   /* 获取终端窗口大小 */
#define TIOCSWINSZ   0x5414   /* 设置终端窗口大小 */

struct winsize {
    uint16_t ws_row;      /* 行数 */
    uint16_t ws_col;      /* 列数 */
    uint16_t ws_xpixel;   /* 水平像素数（通常不用） */
    uint16_t ws_ypixel;   /* 垂直像素数（通常不用） */
};

#ifdef __cplusplus
extern "C" {
#endif

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IOCTL_H */