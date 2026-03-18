#ifndef _TIME_H
#define _TIME_H

typedef long time_t;

#ifdef __cplusplus
extern "C" {
#endif

/* kilo 用 time(NULL) 获取当前秒数，用于状态栏消息的超时。
 * 内核侧可以用 PIT tick 计数 / HZ 来实现，不需要真实 RTC。 */
time_t time(time_t *tloc);

#ifdef __cplusplus
}
#endif

#endif /* _TIME_H */