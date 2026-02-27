/* boot.s */
.section .multiboot, "a"

.global page_directory
.global stack_bottom

.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDEOMODE, 1<<2        /* 新增：请求视频模式标志位 */
.set FLAGS,    ALIGN | MEMINFO | VIDEOMODE
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
/* Multiboot 头部特定地址字段 (如果不是 ELF 格式才需要，这里填 0) */
.long 0, 0, 0, 0, 0

/* 图形模式请求设置 */
.long 0                     /* 0 = 线性图形模式 */
.long 1024                  /* 宽度 */
.long 768                   /* 高度 */
.long 32                    /* 像素深度 (32-bit: R,G,B,Alpha) */

.section .bss
.align 16
stack_bottom:
.skip 32768
stack_top:

.section .data
.align 4096
# 创建足够的页表来映射至少 8MB（或更多）
page_table_low:
    .set i, 0
    .rept 1024
        .long (i << 12) | 0x3
        .set i, i + 1
    .endr

page_table_high:
    .set i, 1024
    .rept 1024
        .long (i << 12) | 0x3
        .set i, i + 1
    .endr

.align 4096
page_directory:
    # 恒等映射：0-4MB
    .long (page_table_low - 0xC0000000) + 0x3
    
    # 映射 4MB-8MB（如果需要）
    .long (page_table_high - 0xC0000000) + 0x3
    
    # 填充中间的空白
    .fill 766, 4, 0
    
    # 高半区映射：0xC0000000-0xC0400000 (0-4MB)
    .long (page_table_low - 0xC0000000) + 0x3
    
    # 高半区映射：0xC0400000-0xC0800000 (4MB-8MB)
    .long (page_table_high - 0xC0000000) + 0x3
    
    .fill 254, 4, 0

.section .scaffold
.global _start
.type _start, @function
_start:
    cli
    movl $(page_directory - 0xC0000000), %eax
    movl %eax, %cr3
    movl %cr0, %eax
    or $0x80000000, %eax
    movl %eax, %cr0
    movl $_tokernelmain, %eax
    jmp *%eax

.section .text
.global _tokernelmain
.type _tokernelmain, @function
_tokernelmain:
    mov $stack_top, %esp
    /* 关键：将 Multiboot 信息结构的地址 (存在 ebx 中) 压入栈，传给 C++ */
    push %ebx
    call _init
    call kernel_main

    cli
hltLoop:
    hlt
    jmp hltLoop
