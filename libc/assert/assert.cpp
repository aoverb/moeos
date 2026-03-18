#include <assert.h>
#include <stdio.h>
void __assert_fail(const char* expr, const char* file, int line) {
    printf("ASSERT FAIL: %d ", 13);
    printf("%s \n", expr);
    for (;;) asm volatile("hlt");
}