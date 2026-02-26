SECTIONS
{
    /* 我们的用户态加载程序的基址就是这个地方 */
    . = 0x10000000;

    .text :
    {
        hello_world.o(.text) /* 把我们的hello_world放在最头部 */
        *(.text)
    }

    .rodata :
    {
        *(.rodata)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        *(.bss)
    }
}