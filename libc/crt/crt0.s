.section .text.entry
.global _start
.extern main

_start:
    call main
    mov $0, %eax
    int $0x80
    ret
