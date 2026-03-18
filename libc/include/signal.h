#ifndef _SIGNAL_DEF_H
#define _SIGNAL_DEF_H 1
#include <stdint.h>
#include <register.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif
#define SIGHUP       1
#define SIGINT       2
#define SIGQUIT      3
#define SIGILL       4
#define SIGABRT      6
#define SIGKILL      9
#define SIGSEGV     11
#define SIGPIPE     13
#define SIGALRM     14
#define SIGTERM     15
#define SIGCHLD     17
#define SIGCONT     18
#define SIGSTOP     19
#define SIGWINCH    28    /* 终端窗口大小变化 */

using pid_t = int;
typedef void (*signal_handler_t)(registers*, pid_t);

#ifdef __cplusplus
}
#endif

#endif
