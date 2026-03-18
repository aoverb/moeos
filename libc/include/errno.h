#ifndef _ERRNO_H
#define _ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif

extern int errno;

#define EPERM        1   /* 操作不允许 */
#define ENOENT       2   /* 文件或目录不存在 */
#define ESRCH        3   /* 进程不存在 */
#define EINTR        4   /* 系统调用被信号中断 */
#define EIO          5   /* I/O 错误 */
#define ENXIO        6   /* 设备不存在 */
#define E2BIG        7   /* 参数列表过长 */
#define ENOEXEC      8   /* 可执行格式错误 */
#define EBADF        9   /* 无效文件描述符 */
#define ECHILD      10   /* 没有子进程 */
#define EAGAIN      11   /* 资源暂时不可用 */
#define ENOMEM      12   /* 内存不足 */
#define EACCES      13   /* 权限不足 */
#define EFAULT      14   /* 无效地址 */
#define EBUSY       16   /* 设备或资源忙 */
#define EEXIST      17   /* 文件已存在 */
#define EXDEV       18   /* 跨设备链接 */
#define ENODEV      19   /* 设备不存在 */
#define ENOTDIR     20   /* 不是目录 */
#define EISDIR      21   /* 是目录 */
#define EINVAL      22   /* 无效参数 */
#define ENFILE      23   /* 系统文件表溢出 */
#define EMFILE      24   /* 打开的文件过多 */
#define ENOTTY      25   /* 不是终端设备 */
#define EFBIG       27   /* 文件过大 */
#define ENOSPC      28   /* 磁盘空间不足 */
#define ESPIPE      29   /* 非法 seek */
#define EROFS       30   /* 只读文件系统 */
#define EPIPE       32   /* 管道破裂 */
#define EDOM        33   /* 数学参数超出范围 */
#define ERANGE      34   /* 结果超出范围 */
#define ENOSYS      38   /* 系统调用未实现 */
#define ENOTEMPTY   39   /* 目录非空 */

#ifdef __cplusplus
}
#endif

#endif /* _ERRNO_H */