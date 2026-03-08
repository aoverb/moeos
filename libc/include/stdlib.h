#ifndef _STDLIB_H
#define _STDLIB_H 1

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* malloc(size_t size);
void  free(void* ptr);
void* sbrk(uintptr_t increment);
int abs(int n);
long labs(long n);

#ifdef __cplusplus
}
#endif

#endif