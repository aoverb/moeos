CC := i686-elf-gcc
CXX := i686-elf-g++
AS := i686-elf-as

CFLAGS := -O2 -g -ffreestanding -Wall -Wextra --sysroot=$(SYSROOT) -isystem $(SYSROOT)/usr/include
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti
LDFLAGS := -ffreestanding -O2 -nostdlib

LIBC := $(SYSROOT)/usr/lib/libc.a
CRT0 := $(SYSROOT)/usr/lib/crt0.o
LINKER_LD := ../linker.ld

# PROG_NAME 和 OBJS 由各子目录的 Makefile 定义
all: $(PROG_NAME)

$(PROG_NAME): $(PROG_NAME).elf
	objcopy -O binary $< $@

$(PROG_NAME).elf: $(OBJS) $(LINKER_LD) $(LIBC) $(CRT0)
	$(CC) -T $(LINKER_LD) -o $@ $(LDFLAGS) $(OBJS) $(CRT0) -L$(SYSROOT)/usr/lib -lc -lgcc

%.o: %.c
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS)

%.o: %.cpp
	$(CXX) -MD -c $< -o $@ -std=gnu++11 $(CXXFLAGS)

install-bin: $(PROG_NAME)
	cp -R --preserve=timestamps $(PROG_NAME) $(SYSROOT)/usr/bin/

clean:
	rm -f $(PROG_NAME) $(PROG_NAME).elf $(OBJS) $(OBJS:.o=.d)

-include $(OBJS:.o=.d)