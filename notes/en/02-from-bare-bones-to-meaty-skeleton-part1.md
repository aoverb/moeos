## Homemade OS (2): From Bare Bones to Meaty Skeleton (Part 1)

In the previous section, we used GRUB to implement the bootloader and successfully printed "Hello World" on screen.

![image-20260127201726165](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%882%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8A%EF%BC%89/image-20260127201726165.png)

Even though the code was written with AI, we've successfully tackled the tedious task of configuring the build environment! Congratulations to us.

But take a look at our config directory — the files are quite messy.

![image-20260127201815512](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%882%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8A%EF%BC%89/image-20260127201815512.png)

We should reorganize our files with some structure. While there aren't many files now, the number will grow as we develop the OS, so we need to plan ahead. Let's organize our files using the following structure:

## Target Structure: The "Four Pillars" of Meaty Skeleton

We'll split the project into four core logical areas. This structure mimics the organization of Unix-like systems:

| **Directory**   | **Role**              | **Responsibility**                                        |
| --------------- | --------------------- | --------------------------------------------------------- |
| **`kernel/`**   | **Kernel Core**       | Stores all code that runs in kernel mode.                 |
| **`libc/`**     | **Common Utilities**  | Stores standard functions shared by kernel and user space. |
| **`include/`**  | **Global Contract**   | Public headers defining interfaces between kernel and external. |
| **`arch/`**     | **Hardware Translator**| Architecture-specific code (e.g., i386, ARM).             |

## The Great Migration

Now we need to disassemble the Bare Bones parts and fit them into the new skeleton.

### Step 1: Relocate Kernel Entry

Move the assembly code to the architecture directory, as it's tightly coupled to x86 boot protocol.

- **Old location**: `boot.S`
- **New location**: `kernel/arch/i386/boot.S`

### Step 2: Isolate Platform Code

Move all C code that directly manipulates hardware (e.g., VGA framebuffer) into the architecture directory.

- **Old location**: Text output code in `kernel.cpp`
- **New location**: `kernel/arch/i386/tty.c`

### Step 3: Define Kernel Core

Keep platform-independent logic in the kernel directory.

- **New location**: `kernel/kernel/kernel.cpp` (just the `kernel_main` logic here)

### Step 4: Relocate Linker Script

The linker script describes memory layout and is highly architecture-dependent.

- **New location**: `kernel/arch/i386/linker.ld`

In a nutshell, we're separating architecture-independent and architecture-dependent modules for easier management later.

Use the following script for a one-click migration:

```shell
mkdir -p kernel/arch/i386
mkdir -p kernel/kernel
mv boot.s kernel/arch/i386/boot.s
mv kernel.cpp kernel/kernel/kernel.cpp
mv linker.ld kernel/arch/i386/linker.ld
```

### Key Point: What is the `arch/i386` Directory?

This is where we put architecture-specific implementations.

Simply put, `arch/i386` currently stores code related to booting the system and VGA output for the i386 architecture. If we later want to port to ARM, we'd create a corresponding folder and implement the appropriate code there. The kernel's `kernel_main` doesn't care about the architecture — it just calls the appropriate interfaces. (Interface-implementation separation.)

## Sharpening the Axe: Separating the Graphics Driver

Before moving forward, we need decoupling not just at the file level but also at the code level. Our current text display logic is actually architecture-dependent, so we need to split it off.

Create these two files:

```
kernel/include/kernel/tty.h
kernel/arch/i386/tty.cpp
```

This again reflects interface-implementation separation.

```cpp
#ifndef _KERNEL_TTY_H
#define _KERNEL_TTY_H

#include <stdint.h>
#include <boot/multiboot.h>

#ifdef __cplusplus
extern "C" {
#endif

void terminal_initialize(multiboot_info_t* mbi);
void terminal_putpixel(int x, int y, uint32_t color);
void terminal_draw_char(int x, int y, const uint8_t* font_char, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif
```

`tty.cpp` implements the code originally in `kernel.cpp`.

We also need to create `kernel/include/boot/multiboot.h` to extract `struct multiboot_info_t`, since future memory management modules (PMM/VMM) will also need it — it can't be exclusive to the graphics driver.

## Makefile

After reorganizing our files, a new problem emerges: our old build script no longer works!

![image-20260127203124157](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%882%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8A%EF%BC%89/image-20260127203124157.png)

Our old build script was too simple. Next, we'll introduce a **Makefile automation system** to stitch together the scattered modules using the power of makefiles.

### The Problem: Directory Depth and Path Hell

In the Bare Bones stage, you only needed a few commands like `i686-elf-gcc -c kernel.c ...`. But now, your files are distributed like this:

- `kernel/arch/i386/boot.S`
- `kernel/arch/i386/tty.c`
- `kernel/kernel/kernel.cpp`

**We need to solve three core problems:**

1. **Lookup problem**: The compiler doesn't know where to find headers at different depths.
2. **Incremental build problem**: If I only modified `tty.c`, why should I recompile the entire kernel?
3. **Maintenance problem**: Do I have to manually edit the build script every time I add a feature?

To solve these three problems, we adopt a divide-and-conquer approach:

1. Generate corresponding object files under each directory structure.
2. If files under a directory haven't been modified since the last build, skip recompilation.
3. Automatically scan the project structure and execute the corresponding build commands.

This is exactly what Makefiles can do for us.

### Divide and Conquer

We can create a makefile in each subdirectory and use several scripts at the OS root to store our configuration and overall build logic.

------

#### Architecture Configuration (`kernel/arch/i386/make.config`)

This file is the core of "divide and conquer." It simply tells the main Makefile which extra object files (`.o`) and special parameters exist for the i386 architecture.

**File: `kernel/arch/i386/make.config`**

```makefile
# Define the list of .o files to compile for this architecture
KERNEL_ARCH_OBJS=\
$(ARCHDIR)/boot.o \
$(ARCHDIR)/tty.o
```

------

#### Top-Level Makefile (`kernel/Makefile`)

This is the most hardcore part. It dynamically builds by "including" the configuration files above.

**File: `kernel/Makefile`**

```makefile
# 1. Basic configuration
HOSTARCH:=i386
ARCHDIR:=arch/$(HOSTARCH)

# Compiler settings (assuming you have i686-elf-gcc configured)
CC:=i686-elf-gcc
CXX:=i686-elf-g++
AS = i686-elf-as

# Key compilation flags
# -ffreestanding: bare-metal environment, no standard library
# -Iinclude: tell compiler to look for headers in kernel/include, not system
CFLAGS:=-O2 -g -ffreestanding -Wall -Wextra -Iinclude
CXXFLAGS:=$(CFLAGS) -fno-exceptions -fno-rtti
LDFLAGS:=-ffreestanding -O2 -nostdlib

# 2. Include architecture-specific part list
# This imports the variable $(KERNEL_ARCH_OBJS)
include $(ARCHDIR)/make.config

# 3. Combine all parts
# All .o files = architecture-related + kernel core
KERNEL_OBJS=\
$(KERNEL_ARCH_OBJS) \
kernel/kernel.o \

# 4. Build rules

# Target: final kernel file
myos.kernel: $(KERNEL_OBJS) $(ARCHDIR)/linker.ld
        $(CC) -T $(ARCHDIR)/linker.ld -o $@ $(LDFLAGS) $(KERNEL_OBJS) -lgcc

# Auto-derivation rules: how to go from .c to .o
%.o: %.c
        $(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS)

# Auto-derivation rules: how to go from .cpp to .o
%.o: %.cpp
        $(CXX) -MD -c $< -o $@ -std=gnu++11 $(CXXFLAGS)

# Auto-derivation rules: how to go from .S to .o
%.o: %.s
        $(AS) --32 -o $@ $<
		
# Clean
clean:
        rm -f myos.kernel
        rm -f $(KERNEL_OBJS)
        rm -f $(KERNEL_OBJS:.o=.d)
```

### make

With all of this ready, we can modify `build.sh`:

```shell
#!/bin/bash
cd kernel
make
cd ..
# Copy kernel
cp kernel/myos.kernel isodir/boot/myos.bin

# Create GRUB config
cat > isodir/boot/grub/grub.cfg << EOF
menuentry "My First OS" {
        multiboot /boot/myos.bin
}
EOF

# Generate ISO
grub-mkrescue -o myos.iso isodir
qemu-system-i386 -cdrom myos.iso
```

Then execute it, and we can see the build process — our refactored OS runs successfully.

![image-20260128001533591](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%882%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8A%EF%BC%89/image-20260128001533591.png)
