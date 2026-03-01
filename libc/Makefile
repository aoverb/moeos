CXX:=i686-elf-g++
CC:=i686-elf-gcc
AR:=i686-elf-ar

LIBC_OBJS=\
stdio/printf.o \
stdio/putchar.o \
stdio/puts.o \
cstring/strlen.o \
cstring/memcpy.o \
cstring/memset.o \
cstring/strcmp.o \
cstring/strcpy.o \
cstring/strcpy.o \
string/string.o \
string/allocator.o \
file/file.o \
sys/wait.o \
stdio/setcolor.o \
stdio/getline.o

SYSROOT?=

CFLAGS?=-O2 -g
CPPFLAGS?=

CFLAGS:=$(CFLAGS) -ffreestanding -Wall -Wextra -fno-exceptions -fno-rtti
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

all: $(BINARIES) crt0.o

libc.a: $(LIBC_OBJS)
	$(AR) rcs $@ $(LIBC_OBJS)

%.libc.o: %.c
	$(CC) -MD -c $< -o $@ -std=gnu11 $(LIBC_CFLAGS) $(LIBC_CPPFLAGS)

%.libc.o: %.cpp
	$(CXX) -MD -c $< -o $@ -std=c++17 $(LIBC_CFLAGS) $(LIBC_CPPFLAGS)

crt0.o: crt/crt0.s
	$(AS) --32 -o $@ $<

libk.a: $(LIBK_OBJS)
	$(AR) rcs $@ $(LIBK_OBJS)

%.libk.o: %.c
	$(CC) -MD -c $< -o $@ -std=gnu11 $(LIBK_CFLAGS) $(LIBK_CPPFLAGS)

%.libk.o: %.cpp
	$(CXX) -MD -c $< -o $@ -std=c++17 $(LIBK_CFLAGS) $(LIBK_CPPFLAGS)

-include $(LIBC_OBJS:.o=.d)
-include $(LIBK_OBJS:.o=.d)

install-headers:
	cp -R --preserve=timestamps include/. $(SYSROOT)/usr/include/.

install-libc:
	cp -R --preserve=timestamps $(BINARIES) $(SYSROOT)/usr/lib/
	cp -R --preserve=timestamps crt0.o $(SYSROOT)/usr/lib/

clean:
	rm -f $(LIBK_OBJS)
	rm -f $(LIBK_OBJS:.o=.d)
	rm -f $(LIBC_OBJS)
	rm -f $(LIBC_OBJS:.o=.d)