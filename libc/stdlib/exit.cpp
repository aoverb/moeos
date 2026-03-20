#include <stdlib.h>
#include <syscall_def.hpp>

int errno = 0;
extern "C" void __gxx_personality_v0() {} // todo: 临时规避

int _exit(int status) {
    return syscall1((uint32_t)SYSCALL::EXIT, (uint32_t)status);
}

int atexit(atexit_func func) {
    if (atexit_count >= 32) return -1;
    atexit_handlers[atexit_count++] = func;
    return 0;
}

// exit — 先逆序执行 atexit 注册的回调，再调 _exit 系统调用
void exit(int status) {
    for (int i = atexit_count - 1; i >= 0; i--) {
        atexit_handlers[i]();
    }
    _exit(status);
}

void abort() {
    exit(1);
}

int system(const char *command) { // stub
    (void)command;
    return -1;
}

void yield() {
    syscall0((uint32_t)SYSCALL::YIELD);
    return;
}