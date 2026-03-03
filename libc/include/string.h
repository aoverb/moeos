#ifndef _STRING_H
#define _STRING_H 1

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t strlen(const char*);
int strcmp(const char*, const char*);
int strncmp(const char* str1, const char* str2, size_t n);
void* memcpy(void* __restrict, const void* __restrict, size_t);
void* memset(void*, int, size_t);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strchr(const char* s, int c);

#ifdef __cplusplus
}
#endif

#endif
