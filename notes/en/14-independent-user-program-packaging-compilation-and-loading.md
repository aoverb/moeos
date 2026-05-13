## Homemade OS (14): Independent User Program Packaging, Compilation and Loading

In the previous chapter, we finally moved a small function to user mode. But that function couldn't even call libc functions! It's time to separate our user mode entirely.

### Independent User-Mode Programs — Simple Example

Our journey can't stop at Hello world. Next, we'll independently compile user-mode binary programs, load them into memory using GRUB Module, and create processes by reading the memory contents from `kernel_main`. Let's start with a minimal program to achieve separate compilation, then gradually improve it.

#### Independent Compilation Setup

It's time to create a new `user` directory in our root directory to store user-mode program source files. Let's start with a Makefile:

```makefile
# Compiler settings (assuming you've set up i686-elf-gcc)
CC:=i686-elf-gcc
CXX:=i686-elf-g++
AS = i686-elf-as

# Key compilation flags
# -ffreestanding: bare-metal environment, no standard library
CFLAGS:=-O0 -g -ffreestanding -Wall -Wextra --sysroot=$(SYSROOT) -isystem $(SYSROOT)/usr/include
CXXFLAGS:=$(CFLAGS) -fno-exceptions -fno-rtti # C++ needs exceptions and RTTI disabled
LDFLAGS:=-ffreestanding -O0 -nostdlib

USER_OBJS=\
hello_world.o

LIBC:=$(SYSROOT)/usr/lib/libc.a

all: hello_world.bin

# Target: Our Hello world.bin
hello_world.bin: $(USER_OBJS) ./linker.ld $(LIBC)
	$(CC) -T ./linker.ld -o $@ $(LDFLAGS) $(USER_OBJS) -L$(SYSROOT)/usr/lib -lc -lgcc

# Automatic derivation rule: how to go from .c to .o
%.o: %.c
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS)

# Automatic derivation rule: how to go from .cpp to .o
%.o: %.cpp
	$(CXX) -MD -c $< -o $@ -std=gnu++11 $(CXXFLAGS)

install-bin:
	cp -R --preserve=timestamps ./hello_world.bin $(SYSROOT)/usr/bin/.

# Clean
clean:
	rm -f hello_world.bin
	rm -f $(USER_OBJS)
	rm -f $(USER_OBJS:.o=.d)

-include $(USER_OBJS:.o=.d)
```

Let's prepare a simple user-mode program:

```cpp
#include <stdint.h>

void main() {
    uint32_t ret;
    const char* s = "Hello world from a independant program!\n";
    asm volatile("int $0x80" : "=a"(ret) : "a"(1), "b"(s));
    asm volatile("int $0x80" : "=a"(ret) : "a"(0));
}
```

Write a corresponding linker script:

```linker
SECTIONS
{
    /* Our user-mode program base address is here */
    . = 0x10000000;

    .text :
    {
        hello_world.o(.text) /* Put hello_world at the very beginning */
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
```

Add a build script and execute it:

```shell
#!/bin/bash
export SYSROOT=`pwd`/SYSROOT
echo $SYSROOT
mkdir -p $SYSROOT/usr/bin
cd user
make all install-bin
cd ..
```

![image-20260226183336931](../assets/自制操作系统（14）：独立用户态程序封装、编译与加载/image-20260226183336931.png)

Oops — our previous `build_libc` script could only build `libk.a` (yes, you read that right, and I didn't misspell it). So we need to modify the Makefile to also compile `libc`.

```makefile
CFLAGS:=$(CFLAGS) -ffreestanding -Wall -Wextra
CPPFLAGS:=$(CPPFLAGS) -D__is_libc --sysroot=$(SYSROOT) -isystem $(SYSROOT)/usr/include
LIBK_CFLAGS:=$(CFLAGS)
LIBK_CPPFLAGS:=$(CPPFLAGS) -D__is_libk
LIBC_CFLAGS:=$(CFLAGS)
LIBC_CPPFLAGS:=$(CPPFLAGS)

LIBK_OBJS=$(LIBC_OBJS:.o=.libk.o)
BINARIES=\
libk.a \
libc.a

.PHONY: all clean install-headers

all: $(BINARIES)

libc.a: $(LIBC_OBJS)
	$(AR) rcs $@ $(LIBC_OBJS)

%.libc.o: %.c
	$(CC) -MD -c $< -o $@ -std=gnu11 $(LIBC_CFLAGS) $(LIBC_CPPFLAGS)

...

-include $(LIBC_OBJS:.o=.d)
-include $(LIBK_OBJS:.o=.d)

...

install-libc:
	cp -R --preserve=timestamps $(BINARIES) $(SYSROOT)/usr/lib/

clean:
	rm -f $(LIBK_OBJS)
	rm -f $(LIBK_OBJS:.o=.d)
	rm -f $(LIBC_OBJS)
	rm -f $(LIBC_OBJS:.o=.d)
```

After these configurations, we can successfully compile `hello_world.bin`.

#### GRUB Module

We need to load the compiled binary via GRUB Module. First, package it into the disk image:

```shell
# Copy user-mode program
cp user/hello_world.bin isodir/boot/hello_world.bin

# Create GRUB configuration file
cat > isodir/boot/grub/grub.cfg << EOF
menuentry "LoliOS" {
	multiboot /boot/lolios.bin
    module /boot/hello_world.bin
}
EOF
```

Then write a simple piece of logic in `kernel_main` to read the module and create a user-mode process with the file content:

```cpp

typedef struct {
    uint32_t mod_start;   // Module start physical address in memory
    uint32_t mod_end;     // Module end physical address
    uint32_t cmdline;     // Module command line string (the path in grub.cfg)
    uint32_t pad;         // Reserved, always 0
} multiboot_module_t;

extern "C" void kernel_main(multiboot_info_t* mbi) {
    ...

    if (mbi->flags & (1 << 3)) {  // Check if mods field is valid
        multiboot_module_t* mods = (multiboot_module_t*)mbi->mods_addr;
        uint32_t mod_count = mbi->mods_count;

        for (uint32_t i = 0; i < mod_count; i++) {
            void* start = (void*)mods[i].mod_start;
            size_t size = mods[i].mod_end - mods[i].mod_start;
            const char* name = (const char*)mods[i].cmdline;

            create_user_process(start, size, 1);
        }
    }

	...
}
```

After running, we got a Page Fault. It seems our loading logic still has issues.

![image-20260226192743864](../assets/自制操作系统（14）：独立用户态程序封装、编译与加载/image-20260226192743864.png)

Looking at it in GDB, the instructions didn't seem right — like it was reading wrong data. After asking Claude, I realized I was generating an ELF file, not a flat binary... We need to modify the Makefile:

```makefile
# Target: Our Hello world.bin
hello_world.bin: hello_world.elf
	objcopy -O binary hello_world.elf hello_world.bin

hello_world.elf: $(USER_OBJS) ./linker.ld $(LIBC)
	$(CC) -T ./linker.ld -o $@ $(LDFLAGS) $(USER_OBJS) -L$(SYSROOT)/usr/lib -lc -lgcc
```

![image-20260226194154773](../assets/自制操作系统（14）：独立用户态程序封装、编译与加载/image-20260226194154773.png)

Successfully running now. Let's try the syscall function that didn't work in the previous chapter:

```cpp
#include <stdint.h>
#include <syscall_def.h>

void main() {
    uint32_t ret;
    const char* s = "Hello world from a independant program!\n";
    syscall1(1, reinterpret_cast<uint32_t>(s));
    syscall0(0);
}
```

Ran it, and still got error #14. GDB debugging:

![image-20260226195751469](../assets/自制操作系统（14）：独立用户态程序封装、编译与加载/image-20260226195751469.png)

It turns out that the code at the very beginning of `.text` was `syscall1`! I was careless — I overlooked the impact of including header files.

#### Simple Wrapper

In that case, we need to create a small wrapper at the compilation stage.

Let's write a small assembly snippet in libc to handle process initialization and cleanup:

```assembly
.section .text.entry
.global _start
.extern main

_start:
    call main
    mov $0, %eax
    int $0x80
    ret

```

This is the classic **crt0** (C Runtime 0).

Add a rule in the Makefile to compile the assembly:

```makefile
all: $(BINARIES) crt0.o

crt0.o: crt/crt0.s
	$(AS) --32 -o $@ $<
```

There's a pitfall here: you can't link `crt0.o` as part of libc using `ld`, because `ld` only links symbols that are currently unresolved. The `_start` symbol we defined won't be used until the user program links against `libc.a`. So we need to compile it separately as `crt0.o`.

```makefile
CRT0:=$(SYSROOT)/usr/lib/crt0.o

...

hello_world.elf: $(USER_OBJS) ./linker.ld $(LIBC) $(CRT0)
	$(CC) -T ./linker.ld -o $@ $(LDFLAGS) $(USER_OBJS) $(CRT0) -L$(SYSROOT)/usr/lib -lc -lgcc

```

As mentioned above, generating `hello_world.elf` requires separately linking `crt0.o`.

Now modify the linker script to set the program entry point to `_start` (not very useful currently, but convenient for future ELF loading), and place it at the beginning of `.text`:

```linker
ENTRY(_start)

SECTIONS
{
    /* Our user-mode program base address is here */
    . = 0x10000000;

    .text :
    {
         
        *(.text.entry) /* Entry start goes at the very beginning */
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
```

Now we can see the correct result: the user-mode program properly calls the syscall function to output a string and correctly exits:

![image-20260226202754578](../assets/自制操作系统（14）：独立用户态程序封装、编译与加载/image-20260226202754578.png)

A cause for celebration!

### Extracting the Shell

It's time to do something we've been wanting to do for a while... yes, this time we'll move the Shell to user mode. For efficient migration, we'll remove some of the shell's original commands.

```cpp
enum class SYSCALL {
    EXIT = 0,
    TERMINAL_WRITE = 1,
    TERMINAL_SET_TEXT_COLOR = 2,
    TERMINAL_GET_LINE = 3
};
```

Let's implement these system calls first.

```cpp
uint32_t sys_terminal_set_text_color(interrupt_frame* reg) {
    terminal_setcolor(reg->ebx);
    return 0;
}

uint32_t sys_terminal_getline(interrupt_frame* reg) {
    getline(reinterpret_cast<char*>(reg->ebx), reg->ecx);
    return 0;
}

void syscall_init() {
    register_syscall(uint32_t(SYSCALL::EXIT), sys_exit);
    register_syscall(uint32_t(SYSCALL::TERMINAL_WRITE), sys_terminal_write);
    register_syscall(uint32_t(SYSCALL::TERMINAL_SET_TEXT_COLOR), sys_terminal_set_text_color);
    register_syscall(uint32_t(SYSCALL::TERMINAL_GET_LINE), sys_terminal_getline);
}
```

#### Using set_color as an Example

```cpp
#include <stdio.h>
#include <syscall_def.h>
#if defined(__is_libk)
#include <kernel/tty.h>
#endif

void set_color(uint32_t color) {
#if defined(__is_libk)
	terminal_setcolor(color);
#else
	syscall1(2, color);
#endif
}
```

We can use compilation flags to change macros, allowing the same source file to compile both libc and libk libraries!

The final migrated shell running result:

![image-20260226213653648](../assets/自制操作系统（14）：独立用户态程序封装、编译与加载/image-20260226213653648.png)

This is one small step for the shell process... but one giant leap for our operating system!

---

### Summary

In this chapter, we implemented the packaging, compilation, and loading of user-mode programs, improved system calls and the libc library and crt0, and migrated the shell to user space. Although the functionality is reduced, the future looks promising! Our user-mode files can currently only load flat binaries (essentially instruction streams). Later, we can consider parsing real ELF format files.

Starting from the next chapter, we'll unveil the mystery of file systems... We'll begin with an initrd file system based on tar files, building the basic framework of VFS, then focus on the backend and switch to the EXT file system. See you next time!
