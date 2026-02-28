#include <string>

int strcmp(const char* str1, const char* str2) {
    while(*str1 == *str2) {
        if (*str1 == '\0' || *str2 == '\0') break;
        ++str1;
        ++str2;
    }
    if (*str1 == *str2) return 0;
    return *str2 - *str1;
}

int strncmp(const char* str1, const char* str2, size_t n) {
    if (n == 0) return 0;
    while (n-- > 1 && *str1 == *str2 && *str1 != '\0') {
        ++str1;
        ++str2;
    }
    return (unsigned char)*str1 - (unsigned char)*str2;
}