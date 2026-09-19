# SPDX-License-Identifier: GPL-2.0-or-later OR MIT
CC      = gcc
OBJCOPY = objcopy

# -MMD -MP emit a .d per object so header edits force a rebuild.
CFLAGS  = -MMD -MP -Os -ffreestanding -fno-stack-protector -fno-builtin \
          -fpie -mno-red-zone -fomit-frame-pointer -fcf-protection=none \
          -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables \
          -Wall -Wno-unused-function -Wno-unused-variable \
          -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
          -Isrc -Isrc/doomgeneric \
          -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -DFEATURE_DG_SOUND \
          -DSAVEDATA_PROBE=$(SAVEDATA_PROBE) -DDOOM_TRACE=$(DOOM_TRACE) \
          -DFLIP_AHEAD=$(FLIP_AHEAD) -DUDP_PROBE=$(UDP_PROBE) \
          -DMULTIPLAYER=$(MULTIPLAYER) \
          -include funcptr_fix.h

# Build switches. `make FLAG=1` turns one on for that build; .buildflags below
# rebuilds the objects a change affects.

# Per-frame state trace plus a Z_CheckHeap walk every 30 frames. Both cost
# real frame time (the trace alone is ~20 UDP packets a frame), so off unless
# something needs chasing.
DOOM_TRACE ?= 0

# Flips allowed in flight before plt_present blocks. 0 keeps CPU work and
# scanout serial. 1 overlaps them for about 5 fps, but with two framebuffers
# the one being drawn into may still be on screen, so it can tear.
FLIP_AHEAD ?= 0

# Savedata mount matrix. Diagnostic only: it unmounts and remounts /savedata0
# several times, which is not something to do on every boot.
SAVEDATA_PROBE ?= 0

# Echoes UDP packets back to their sender, to prove inbound UDP works before the
# netgame port depends on it.
UDP_PROBE ?= 0

# Netgame. Off by default; the net code is ~6k lines of Chocolate Doom.
MULTIPLAYER ?= 0

LDFLAGS = -T linker_split.ld -nostdlib -nostartfiles -static \
          -Wl,--build-id=none -Wl,--no-dynamic-linker -Wl,-z,norelro -no-pie \
          -Wl,--emit-relocs

# PS5 platform sources
SRC_PS5 = src/dg_main.c src/dg_platform.c src/platform.c src/libc_stubs.c src/usb.c \
          src/dg_sound.c src/dg_opl.c src/dg_music.c src/dg_musicmod.c \
          src/dg_save.c src/dg_wadmenu.c src/dg_ftp.c

# Kept out of the link unless switched on, so their strings stay out of .rodata.
ifeq ($(SAVEDATA_PROBE),1)
SRC_PS5 += src/savedata_probe.c
endif

ifeq ($(UDP_PROBE),1)
SRC_PS5 += src/dg_udpprobe.c
endif

ifeq ($(MULTIPLAYER),1)
SRC_NET = src/doomgeneric/net_client.c src/doomgeneric/net_common.c \
          src/doomgeneric/net_io.c src/doomgeneric/net_loop.c \
          src/doomgeneric/net_packet.c src/doomgeneric/net_query.c \
          src/doomgeneric/net_server.c src/doomgeneric/net_structrw.c \
          src/doomgeneric/net_ps5.c src/doomgeneric/net_ps5_gui.c
endif

# DOOM engine sources
SRC_DOOM = src/doomgeneric/dummy.c src/doomgeneric/am_map.c src/doomgeneric/doomdef.c \
           src/doomgeneric/doomstat.c src/doomgeneric/dstrings.c src/doomgeneric/d_event.c \
           src/doomgeneric/d_items.c src/doomgeneric/d_iwad.c src/doomgeneric/d_loop.c \
           src/doomgeneric/d_main.c src/doomgeneric/d_mode.c src/doomgeneric/d_net.c \
           src/doomgeneric/f_finale.c src/doomgeneric/f_wipe.c src/doomgeneric/g_game.c \
           src/doomgeneric/hu_lib.c src/doomgeneric/hu_stuff.c src/doomgeneric/info.c \
           src/doomgeneric/i_cdmus.c src/doomgeneric/i_endoom.c src/doomgeneric/i_joystick.c \
           src/doomgeneric/i_scale.c src/doomgeneric/i_sound.c src/doomgeneric/i_system.c \
           src/doomgeneric/i_timer.c src/doomgeneric/memio.c src/doomgeneric/m_argv.c \
           src/doomgeneric/m_bbox.c src/doomgeneric/m_cheat.c src/doomgeneric/m_config.c \
           src/doomgeneric/m_controls.c src/doomgeneric/m_fixed.c src/doomgeneric/m_menu.c \
           src/doomgeneric/m_misc.c src/doomgeneric/m_random.c src/doomgeneric/p_ceilng.c \
           src/doomgeneric/p_doors.c src/doomgeneric/p_enemy.c src/doomgeneric/p_floor.c \
           src/doomgeneric/p_inter.c src/doomgeneric/p_lights.c src/doomgeneric/p_map.c \
           src/doomgeneric/p_maputl.c src/doomgeneric/p_mobj.c src/doomgeneric/p_plats.c \
           src/doomgeneric/p_pspr.c src/doomgeneric/p_saveg.c src/doomgeneric/p_setup.c \
           src/doomgeneric/p_sight.c src/doomgeneric/p_spec.c src/doomgeneric/p_switch.c \
           src/doomgeneric/p_telept.c src/doomgeneric/p_tick.c src/doomgeneric/p_user.c \
           src/doomgeneric/r_bsp.c src/doomgeneric/r_data.c src/doomgeneric/r_draw.c \
           src/doomgeneric/r_main.c src/doomgeneric/r_plane.c src/doomgeneric/r_segs.c \
           src/doomgeneric/r_sky.c src/doomgeneric/r_things.c src/doomgeneric/sha1.c \
           src/doomgeneric/sounds.c src/doomgeneric/statdump.c src/doomgeneric/st_lib.c \
           src/doomgeneric/st_stuff.c src/doomgeneric/s_sound.c src/doomgeneric/tables.c \
           src/doomgeneric/v_video.c src/doomgeneric/wi_stuff.c src/doomgeneric/w_checksum.c \
           src/doomgeneric/w_file.c src/doomgeneric/w_main.c src/doomgeneric/w_wad.c \
           src/doomgeneric/z_zone.c src/doomgeneric/w_file_stdc.c src/doomgeneric/i_input.c \
           src/doomgeneric/i_video.c src/doomgeneric/doomgeneric.c

SRCS    = $(SRC_PS5) $(SRC_DOOM) $(SRC_NET)
OBJS    = $(SRCS:.c=.o)
TARGET  = doom

# Objects do not record the flags they were built with, so every object depends
# on this stamp. It is rewritten only when the flag set changes, so a repeat
# build with the same flags is still a no-op.
BUILDFLAGS = MULTIPLAYER=$(MULTIPLAYER) SAVEDATA_PROBE=$(SAVEDATA_PROBE) \
             DOOM_TRACE=$(DOOM_TRACE) FLIP_AHEAD=$(FLIP_AHEAD) \
             UDP_PROBE=$(UDP_PROBE)

# The payload, not the ELF: make_payload.py bakes sizes, the relocation table
# and probe words into the runner, so an ELF without a matching doom.lua sends
# the previous build.
all: $(TARGET).lua

# FORCE has to stay below all, or it becomes the default goal.
.buildflags: FORCE
	@echo '$(BUILDFLAGS)' | cmp -s - $@ || \
	  { echo '$(BUILDFLAGS)' > $@; echo "Flags changed, rebuilding: $(BUILDFLAGS)"; }

FORCE:

%.o: %.c .buildflags
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

$(TARGET).lua: $(TARGET).elf make_payload.py
	python3 make_payload.py

# Section images, for hexdumping or diffing two builds.
bins: $(TARGET).elf
	$(OBJCOPY) -O binary -j .text $< $(TARGET)_code.bin
	$(OBJCOPY) -O binary -j .data $< $(TARGET)_data.bin
	@echo "Code: $$(wc -c < $(TARGET)_code.bin) bytes"
	@echo "Data: $$(wc -c < $(TARGET)_data.bin) bytes"

size: $(TARGET).elf
	@objdump -h $< | grep -E 'Idx|\.text|\.data'

check:
	$(MAKE) -C tools check

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) .buildflags $(TARGET).elf $(TARGET).lua \
	      $(TARGET)_code.bin $(TARGET)_data.bin $(TARGET)_relocs.bin

-include $(OBJS:.o=.d)

.PHONY: all bins check clean size FORCE
