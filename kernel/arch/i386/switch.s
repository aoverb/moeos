.extern process_list
.extern cur_process_id
.global process_switch_to

process_switch_to:
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
    movb %bl, cur_process_id
    movl 4(%eax), %esp

    popl %ebp
    popl %edi
    popl %esi
    popl %ebx

    ret
    