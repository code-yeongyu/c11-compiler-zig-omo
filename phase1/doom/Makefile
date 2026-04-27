-include ../Makefile.common

.DEFAULT_GOAL := all

CC ?= gcc
CSTD ?= c11
HEADLESS ?= 1

VENDOR_DIR := vendor/idoom/linuxdoom-1.10
HEADLESS_DIR := headless
BUILD_DIR := build
TARGET := $(BUILD_DIR)/linuxxdoom
FREEDOOM_VERSION := 0.13.0
FREEDOOM_ZIP := $(BUILD_DIR)/freedoom-$(FREEDOOM_VERSION).zip
FREEDOOM_WAD := $(BUILD_DIR)/freedoom1.wad
SMOKE_LOG := qa/qa-smoke.log

CPPFLAGS += -DNORMALUNIX -DDEFAULT_DOOMWADDIR='"."' -I$(VENDOR_DIR)
ifeq ($(HEADLESS),1)
  CPPFLAGS += -DHEADLESS
endif

CFLAGS ?= -std=$(CSTD) -Wall -Wextra -O2 -g
LDLIBS := -lm

DOOM_SRC := \
	doomdef.c doomstat.c dstrings.c i_system.c tables.c f_finale.c \
	f_wipe.c d_main.c d_net.c d_items.c g_game.c m_menu.c m_misc.c \
	m_argv.c m_bbox.c m_fixed.c m_swap.c m_cheat.c m_random.c am_map.c \
	p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c \
	p_map.c p_maputl.c p_plats.c p_pspr.c p_setup.c p_sight.c p_spec.c \
	p_switch.c p_mobj.c p_telept.c p_tick.c p_saveg.c p_user.c r_bsp.c \
	r_data.c r_draw.c r_main.c r_plane.c r_segs.c r_sky.c r_things.c \
	w_wad.c wi_stuff.c v_video.c st_lib.c st_stuff.c hu_stuff.c hu_lib.c \
	s_sound.c z_zone.c info.c sounds.c i_main.c

ifeq ($(HEADLESS),1)
  PLATFORM_SRC := i_video_headless.c i_sound_headless.c i_net_headless.c
else
  DOOM_SRC += i_video.c i_sound.c i_net.c
endif

OBJ := $(DOOM_SRC:%.c=$(BUILD_DIR)/vendor/%.o) $(PLATFORM_SRC:%.c=$(BUILD_DIR)/headless/%.o)

.PHONY: all clean smoke qa print-platform

all: $(TARGET)

$(BUILD_DIR) $(BUILD_DIR)/vendor $(BUILD_DIR)/headless qa:
	mkdir -p $@

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/vendor/%.o: $(VENDOR_DIR)/%.c | $(BUILD_DIR)/vendor
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/headless/%.o: $(HEADLESS_DIR)/%.c | $(BUILD_DIR)/headless
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(FREEDOOM_ZIP): | $(BUILD_DIR)
	curl -L --fail -o $@ https://github.com/freedoom/freedoom/releases/download/v$(FREEDOOM_VERSION)/freedoom-$(FREEDOOM_VERSION).zip

$(FREEDOOM_WAD): $(FREEDOOM_ZIP)
	python3 -c 'import zipfile; z=zipfile.ZipFile("$(FREEDOOM_ZIP)"); n=[x for x in z.namelist() if x.endswith("freedoom1.wad")][0]; open("$(FREEDOOM_WAD)", "wb").write(z.read(n))'

smoke: $(TARGET) $(FREEDOOM_WAD) | qa
	DOOM_HEADLESS_LOG=$(SMOKE_LOG) ./$(TARGET) -iwad $(FREEDOOM_WAD) -timedemo demo1
	test -s $(SMOKE_LOG)
	grep -q "DOOM started" $(SMOKE_LOG)

clean:
	rm -rf $(BUILD_DIR)
