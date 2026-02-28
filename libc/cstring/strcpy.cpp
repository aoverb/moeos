#include <string.h>

char* strcpy(char* dest, const char* src) {
    char* ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

char* strncpy(char* dest, const char* src, size_t n) {
    char* ret = dest;
    while (n > 0 && *src) {
        *dest++ = *src++;
        --n;
    }
    while (n > 0) {
        *dest++ = '\0';
        --n;
    }
    return ret;
}
