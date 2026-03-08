#include <string.h>
#include <stdint.h>
void* memcpy(void* restrict dstptr, const void* restrict srcptr, size_t size) {
    asm volatile (
        "cld\n\t"
        "rep movsb"
        : "+D"(dstptr), "+S"(srcptr), "+c"(size)
        :
        : "memory"
    );
    return dstptr;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1;
    const unsigned char *p2 = s2;

    while (n--) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dest;
}
