# 1. 基础配置
HOSTARCH:=i386
ARCHDIR:=arch/$(HOSTARCH)

# 编译器设置 (假设你已经配置好了 i686-elf-gcc)
CC:=i686-elf-gcc
CXX:=i686-elf-g++
AS = i686-elf-as

# 关键编译参数
# -ffreestanding: 裸机环境，无标准库
CFLAGS:=-O2 -g -ffreestanding -Wall -Wextra --sysroot=$(SYSROOT) -isystem $(SYSROOT)/usr/include
CXXFLAGS:=$(CFLAGS) -fno-exceptions -fno-rtti # C++需要禁用异常和RTTI
LDFLAGS:=-ffreestanding -O2 -nostdlib

# 2. 引入架构特有的零件清单
# 这会导入变量 $(KERNEL_ARCH_OBJS)
include $(ARCHDIR)/make.config

DRIVER_OBJS=\
driver/keyboard.o \
driver/pit.o \
driver/vfs.o \
driver/tarfs.o \
driver/devfs.o \
driver/console.o \
driver/pipefs.o

MM_OBJS=\
mm/pmm_core.o \
mm/vmm.o \
mm/kheap.o

# 3. 合并所有零件
# 所有的 .o 文件 = 架构相关的 + 内核核心的
KERNEL_OBJS=\
$(KERNEL_ARCH_OBJS) \
$(DRIVER_OBJS) \
$(MM_OBJS) \
kernel/kernel.o \
kernel/isr.o \
kernel/panic.o

# 4. 构建规则

LIBK:=$(SYSROOT)/usr/lib/libk.a

CRTBEGIN_OBJ:=$(shell $(CC) $(CFLAGS) -print-file-name=crtbegin.o)
CRTEND_OBJ:=$(shell $(CC) $(CFLAGS) -print-file-name=crtend.o)

# 目标：最终的内核文件
lolios.kernel: $(ARCHDIR)/crti.o $(ARCHDIR)/crtn.o $(KERNEL_OBJS) $(ARCHDIR)/linker.ld $(LIBK)
	$(CC) -T $(ARCHDIR)/linker.ld -o $@ $(LDFLAGS) \
		$(ARCHDIR)/crti.o $(CRTBEGIN_OBJ) \
		$(KERNEL_OBJS) \
		-L$(SYSROOT)/usr/lib -lk -lgcc \
		$(CRTEND_OBJ) $(ARCHDIR)/crtn.o

# 自动推导规则：如何从 .c 变成 .o
%.o: %.c
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS)

# 自动推导规则：如何从 .cpp 变成 .o
%.o: %.cpp
	$(CXX) -MD -c $< -o $@ -std=gnu++11 $(CXXFLAGS)

# 自动推导规则：如何从 .S 变成 .o
%.o: %.s
	$(AS) --32 -o $@ $<

install-headers:
	cp -R --preserve=timestamps include/. $(SYSROOT)/usr/include/.

# 清理
clean:
	rm -f lolios.kernel
	rm -f $(KERNEL_OBJS)
	rm -f $(KERNEL_OBJS:.o=.d)

-include $(KERNEL_OBJS:.o=.d)
