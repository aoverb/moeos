SECTIONS
{
    /* 我们的用户态加载程序的基址就是这个地方 */
    . = 0x10000000;

    .text ALIGN(4K)
    {
        hello_world.o(.text) /* 把我们的hello_world放在最头部 */
        *(.text)
    }

    .rodata ALIGN(4K)
    {
        *(.rodata)
    }

    .data ALIGN(4K)
    {
        *(.data)
    }

    .bss ALIGN(4K)
    {
        *(.bss)
    }
}