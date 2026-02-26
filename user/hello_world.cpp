#include <stdint.h>
#include <syscall_def.h>

void main() {
    uint32_t ret;
    const char* s = "Hello world from a independant program!\n";
    syscall1(1, reinterpret_cast<uint32_t>(s));
    syscall0(0);
}