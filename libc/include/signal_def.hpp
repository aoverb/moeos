#ifndef _SIGNAL_DEF_H
#define _SIGNAL_DEF_H 1
#include <stdint.h>
#include <register.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif
enum class SIGNAL {
    SIGINT = 2
};
using pid_t = int;
typedef void (*signal_handler_t)(registers*, pid_t);

#ifdef __cplusplus
}
#endif

#endif
