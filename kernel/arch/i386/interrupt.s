.section .text

.extern inner_interrupt_handler
.extern inner_syscall_handler
.extern do_signal
.global common_interrupt_handler
.global system_call_handler

.macro ISR_NOERR n
.global isr\n
isr\n:
    pushl $0
    pushl $\n
    jmp common_interrupt_handler
.endm

.macro ISR_ERR n
.global isr\n
isr\n:
    pushl $\n
    jmp common_interrupt_handler
.endm

/* 0  Divide-by-zero */
ISR_NOERR 0
/* 1  Debug */
ISR_NOERR 1
/* 2  NMI */
ISR_NOERR 2
/* 3  Breakpoint */
ISR_NOERR 3
/* 4  Overflow */
ISR_NOERR 4
/* 5  Bound Range Exceeded */
ISR_NOERR 5
/* 6  Invalid Opcode */
ISR_NOERR 6
/* 7  Device Not Available */
ISR_NOERR 7

/* 8  Double Fault (error code) */
ISR_ERR   8

/* 9  Coprocessor Segment Overrun (reserved) */
ISR_NOERR 9

/* 10 Invalid TSS */
ISR_ERR   10
/* 11 Segment Not Present */
ISR_ERR   11
/* 12 Stack-Segment Fault */
ISR_ERR   12
/* 13 General Protection Fault */
ISR_ERR   13
/* 14 Page Fault */
ISR_ERR   14

/* 15 reserved */
ISR_NOERR 15

/* 16 x87 Floating-Point Exception */
ISR_NOERR 16

/* 17 Alignment Check */
ISR_ERR   17

/* 18 Machine Check */
ISR_NOERR 18
/* 19 SIMD Floating-Point Exception */
ISR_NOERR 19
/* 20 Virtualization Exception */
ISR_NOERR 20

/* 21 Control Protection Exception */
ISR_ERR   21

/* 22–27 reserved */
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27

/* 28 Hypervisor Injection Exception */
ISR_NOERR 28

/* 29 VMM Communication Exception */
ISR_ERR   29
/* 30 Security Exception */
ISR_ERR   30

/* 31 reserved */
ISR_NOERR 31

/* 32-47 all hardware interrupts */
ISR_NOERR 32
ISR_NOERR 33
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47

common_interrupt_handler:
    pusha
    # 保存段寄存器
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    pushl %esp
    
    call inner_interrupt_handler
    # do_signal
    call do_signal
    addl $4, %esp
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popa
    addl $8, %esp
    iret

system_call_handler:
    # SS, ESP, EFLAGS, CS, EIP已在特权级切换时被自动保存
    pushl %es
    pushl %ds
    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax

    mov $0x10, %eax
    mov %eax, %ds
    mov %eax, %es

    pushl %esp
    call inner_syscall_handler
    
    addl $4, %esp
    popl %eax
    popl %ebx
    popl %ecx
    popl %edx
    popl %esi
    popl %edi
    popl %ebp
    popl %ds
    popl %es
    iret
