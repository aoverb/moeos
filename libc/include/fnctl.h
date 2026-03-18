#ifndef _FCNTL_H
#define _FCNTL_H

/* open() 文件访问模式 */
#define O_RDONLY     0x0000
#define O_WRONLY     0x0001
#define O_RDWR       0x0002
#define O_ACCMODE    0x0003   /* 访问模式掩码 */
#define O_CREATE     0x0004

/* open() 文件创建标志 */
#define O_CREAT      0x0040   /* 文件不存在则创建 */
#define O_EXCL       0x0080   /* 与 O_CREAT 一起用，文件已存在则失败 */
#define O_TRUNC      0x0200   /* 截断为零长度 */
#define O_APPEND     0x0400   /* 追加模式 */

/* open() 非阻塞 */
#define O_NONBLOCK   0x0800

/* 文件权限位（mode_t，用于 open 第三个参数） */
#define S_IRUSR      0400
#define S_IWUSR      0200
#define S_IRGRP      0040
#define S_IROTH      0004

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif /* _FCNTL_H */