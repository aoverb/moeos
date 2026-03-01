#ifndef _WAIT_H
#define _WAIT_H 1
#include <stdint.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif

int waitpid(int pid);

#ifdef __cplusplus
}
#endif

#endif
