#ifndef _REGISTER_H
#define _REGISTER_H 1
#include <stdint.h>
#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif

struct registers {
    uint32_t gs, fs, es, ds;      // 对应 pop %gs ... pop %ds
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // pusha 压入
    uint32_t int_no, err_code;                       // 我们手动压入
    uint32_t eip, cs, eflags, useresp, ss;           // CPU 自动压入
};

#ifdef __cplusplus
}
#endif

#endif