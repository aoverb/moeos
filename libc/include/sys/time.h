#ifndef _SYS_TIME_H
#define _SYS_TIME_H
 
#include <stdint.h>
 
typedef long time_t;
typedef long suseconds_t;
 
struct timeval {
    time_t      tv_sec;   /* 秒 */
    suseconds_t tv_usec;  /* 微秒 */
};
 
#ifdef __cplusplus
extern "C" {
#endif
 
/* kilo 用 time(NULL) 获取当前秒数 */
time_t time(time_t *tloc);
 
/* gettimeofday 作为备选 */
int gettimeofday(struct timeval *tv, void *tz);
 
#ifdef __cplusplus
}
#endif
 
#endif /* _SYS_TIME_H */