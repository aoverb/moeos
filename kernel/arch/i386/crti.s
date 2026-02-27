.section .init
.global _init
_init:
    push %ebp
    mov %esp, %ebp
    /* gcc 会在这中间插入构造函数调用 */

.section .fini
.global _fini
_fini:
    push %ebp
    mov %esp, %ebp
