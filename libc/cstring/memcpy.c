#include <string.h>

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
