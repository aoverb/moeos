#ifndef _STRINGS_H
#define _STRINGS_H 1

#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, unsigned int n);
#ifdef __cplusplus
}
#endif
#define bzero(p, n) memset(p, 0, n)

#endif