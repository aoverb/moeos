#include <stdint.h>

void main() {
    uint32_t ret;
    const char* s = "Hello world from a independant program!\n";
    asm volatile("int $0x80" : "=a"(ret) : "a"(1), "b"(s));
    asm volatile("int $0x80" : "=a"(ret) : "a"(0));
}