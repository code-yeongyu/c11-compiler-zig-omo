-include ../Makefile.common

.DEFAULT_GOAL := all

CC ?= gcc
CSTD ?= c11
CFLAGS ?= -std=$(CSTD) -Wall -Wextra -O2 -g

VENDOR_DIR := vendor/idoom/linuxdoom-1.10
BUILD_DIR := build/baseline
TARGET := $(BUILD_DIR)/linuxxdoom

CPPFLAGS += -DNORMALUNIX -DLINUX
LDLIBS := -lXext -lX11 -lnsl -lm

SRC := \
	doomdef.c doomstat.c dstrings.c i_system.c i_sound.c i_video.c \
	i_net.c tables.c f_finale.c f_wipe.c d_main.c d_net.c d_items.c \
	g_game.c m_menu.c m_misc.c m_argv.c m_bbox.c m_fixed.c m_swap.c \
	m_cheat.c m_random.c am_map.c p_ceilng.c p_doors.c p_enemy.c \
	p_floor.c p_inter.c p_lights.c p_map.c p_maputl.c p_plats.c \
	p_pspr.c p_setup.c p_sight.c p_spec.c p_switch.c p_mobj.c \
	p_telept.c p_tick.c p_saveg.c p_user.c r_bsp.c r_data.c r_draw.c \
	r_main.c r_plane.c r_segs.c r_sky.c r_things.c w_wad.c wi_stuff.c \
	v_video.c st_lib.c st_stuff.c hu_stuff.c hu_lib.c s_sound.c z_zone.c \
	info.c sounds.c i_main.c

OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean smoke qa print-platform

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: $(VENDOR_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

smoke: all
	./$(TARGET) -devparm -nodraw

qa: smoke

clean:
	rm -rf build
