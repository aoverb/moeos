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
