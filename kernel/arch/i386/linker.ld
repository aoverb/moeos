ENTRY(_start)

SECTIONS
{
	. = 1M;
    .boot BLOCK(4K) : ALIGN(4K)
    {
        *(.multiboot)
        *(.scaffold)
    }
    /* 内核将加载在 1MB 处 */
    . += 0xC0000000;

    .text ALIGN(4K) : AT(ADDR(.text) - 0xC0000000)
    {
        *(.text)
    }

    .init ALIGN(4K) : AT(ADDR(.init) - 0xC0000000)
    {
        *(.init)
    }

    .fini ALIGN(4K) : AT(ADDR(.fini) - 0xC0000000)
    {
        *(.fini)
    }

    .init_array ALIGN(4K) : AT(ADDR(.init_array) - 0xC0000000)
    {
        __init_array_start = .;
        *(.init_array)
        *(SORT(.init_array.*))
        __init_array_end = .;
    }

    .fini_array ALIGN(4K) : AT(ADDR(.fini_array) - 0xC0000000)
    {
        __fini_array_start = .;
        *(.fini_array)
        *(SORT(.fini_array.*))
        __fini_array_end = .;
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xC0000000)
    {
        *(.rodata)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - 0xC0000000)
    {
        *(.data)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - 0xC0000000)
    {
        *(COMMON)
        *(.bss)
    }
    
    _kernel_end = . - 0xC0000000;
}