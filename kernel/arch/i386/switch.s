.extern process_list
.extern cur_process_id
.global process_switch_to
.global ret_to_user_mode

process_switch_to:
    cli
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ebp

    #保存...
    movzbl cur_process_id, %eax
    movl process_list(, %eax, 4), %eax
    movl %esp, 4(%eax)

    #更新cur_process_id和esp寄存器...
    movzbl 20(%esp), %eax
    movl process_list(, %eax, 4), %eax
    movl (%eax), %ebx
    cmpb %bl, (cur_process_id)
    je 1f
    movb %bl, cur_process_id
    movl 4(%eax), %esp

    movl 8(%eax), %eax
    movl %cr3, %ebx
    cmpl %ebx, %eax
    je 1f
    movl %eax, %cr3
1:
    popl %ebp
    popl %edi
    popl %esi
    popl %ebx
    ret

ret_to_user_mode:
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    iret
    