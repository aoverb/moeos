#ifndef _STDLIB_H
#define _STDLIB_H 1

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* realloc(void* ptr, size_t new_size);
void* malloc(size_t size);
void *calloc(size_t count, size_t size);
void  free(void* ptr);
void* sbrk(uintptr_t increment);
int abs(int n);
long labs(long n);
double atof(const char *s);

int _exit(int status);
typedef void (*atexit_func)(void);
static atexit_func atexit_handlers[32];
static int atexit_count = 0;
int atexit(atexit_func func);
void exit(int status);
void abort();
void yield();
int system(const char *command);

#ifdef __cplusplus
}
#endif

#endif