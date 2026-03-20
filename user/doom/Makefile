################################################################
# doomgeneric Makefile for moeos
################################################################

# ---- 从你的构建系统继承（按需修改路径）----
SYSROOT ?= ../../SYSROOT
CC      := i686-elf-gcc
AS      := i686-elf-as

CFLAGS  := -O2 -g -ffreestanding -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
CFLAGS  += --sysroot=$(SYSROOT) -isystem $(SYSROOT)/usr/include
CFLAGS  += -DDOOMGENERIC
CFLAGS  += -DDOOMGENERIC_RESX=1024 -DDOOMGENERIC_RESY=768

LDFLAGS := -ffreestanding -O2 -nostdlib
LIBC    := $(SYSROOT)/usr/lib/libc.a
CRT0    := $(SYSROOT)/usr/lib/crt0.o
LINKER_LD := ../linker.ld

# ---- 目标 ----
OBJDIR := build
OUTPUT := doom

# ---- 核心源文件（不含平台相关）----
SRC_DOOM = \
	dummy.c am_map.c doomdef.c doomstat.c dstrings.c \
	d_event.c d_items.c d_iwad.c d_loop.c d_main.c d_mode.c d_net.c \
	f_finale.c f_wipe.c g_game.c gusconf.c \
	hu_lib.c hu_stuff.c info.c \
	i_cdmus.c i_endoom.c i_joystick.c i_scale.c \
	i_sound.c i_system.c i_timer.c i_input.c i_video.c \
	memio.c m_argv.c m_bbox.c m_cheat.c m_config.c m_controls.c \
	m_fixed.c m_menu.c m_misc.c m_random.c \
	p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c \
	p_map.c p_maputl.c p_mobj.c p_plats.c p_pspr.c p_saveg.c \
	p_setup.c p_sight.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c \
	r_bsp.c r_data.c r_draw.c r_main.c r_plane.c r_segs.c r_sky.c r_things.c \
	sha1.c sounds.c statdump.c st_lib.c st_stuff.c s_sound.c tables.c \
	v_video.c wi_stuff.c w_checksum.c w_file.c w_main.c w_wad.c \
	w_file_stdc.c z_zone.c doomgeneric.c \
	doomgeneric_moeos.c

OBJS = $(addprefix $(OBJDIR)/, $(SRC_DOOM:.c=.o))

# ---- 规则 ----
all: $(OUTPUT)

clean:
	rm -rf $(OBJDIR)
	rm -f $(OUTPUT)

$(OUTPUT): $(OBJS) $(LINKER_LD) $(LIBC) $(CRT0)
	@echo [Linking $@]
	$(CC) -T $(LINKER_LD) -o $@ $(LDFLAGS) $(CRT0) $(OBJS) -L$(SYSROOT)/usr/lib -lc -lgcc

$(OBJS): | $(OBJDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

install-bin: $(OUTPUT)
	cp -R --preserve=timestamps $(OUTPUT) $(SYSROOT)/usr/bin/

$(OBJDIR)/%.o: %.c
	@echo [Compiling $<]
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean