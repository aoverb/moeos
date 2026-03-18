#ifndef _STDIO_H
#define _STDIO_H 1
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif
void set_color(uint32_t color);
int printf(const char* __restrict, ...) __attribute__((format(printf, 1, 2)));
int putchar(int);
int puts(const char*);
bool getline(char* buf, uint32_t size);
void cls();

#ifdef __cplusplus
}
#endif

#endif
