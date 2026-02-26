# 编译器设置 (假设你已经配置好了 i686-elf-gcc)
CC:=i686-elf-gcc
CXX:=i686-elf-g++
AS = i686-elf-as

# 关键编译参数
# -ffreestanding: 裸机环境，无标准库
CFLAGS:=-O0 -g -ffreestanding -Wall -Wextra --sysroot=$(SYSROOT) -isystem $(SYSROOT)/usr/include
CXXFLAGS:=$(CFLAGS) -fno-exceptions -fno-rtti # C++需要禁用异常和RTTI
LDFLAGS:=-ffreestanding -O0 -nostdlib

USER_OBJS=\
hello_world.o

LIBC:=$(SYSROOT)/usr/lib/libc.a

all: hello_world.bin

# 目标：我们的Hello world.bin
hello_world.bin: $(USER_OBJS) ./linker.ld $(LIBC)
	$(CC) -T ./linker.ld -o $@ $(LDFLAGS) $(USER_OBJS) -L$(SYSROOT)/usr/lib -lc -lgcc

# 自动推导规则：如何从 .c 变成 .o
%.o: %.c
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS)

# 自动推导规则：如何从 .cpp 变成 .o
%.o: %.cpp
	$(CXX) -MD -c $< -o $@ -std=gnu++11 $(CXXFLAGS)

install-bin:
	cp -R --preserve=timestamps ./hello_world.bin $(SYSROOT)/usr/bin/.

# 清理
clean:
	rm -f hello_world.bin
	rm -f $(USER_OBJS)
	rm -f $(USER_OBJS:.o=.d)

-include $(USER_OBJS:.o=.d)
