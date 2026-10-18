/*
 * Mini65 - Small 6502 simulator with Atari 8bit bios.
 * Copyright (C) 2017-2019 Daniel Serpell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#pragma once

#include "sim65.h"

// Emulated Atari OS variables used by the system, CIO and editor.
#define LMARGN 0x52
#define RMARGN 0x53
#define ROWCRS 0x54
#define COLCRS 0x55
#define DINDEX 0x57
#define SAVMSC 0x58
#define OLDROW 0x5A
#define OLDCOL 0x5B
#define OLDCHR 0x5D
#define OLDADR 0x5E
#define LOGCOL 0x63
#define ADRESS 0x64
#define RAMTOP 0x6A
#define ESCFLG 0x02A2
#define TABMAP 0x02A3
#define LOGMAP 0x02B2
#define SHFLOK 0x02BE
#define RUNAD  0x02E0
#define INITAD 0x02E2
#define MEMTOP 0x02E5
#define MEMLO  0x02E7
#define CRSINH 0x02F0
#define CH     0x02FC
#define CH1    0x02F2
#define DSPFLG 0x02FE

// Option flags
enum atari_opts_flags
{
    // Flag to skip the DOS emulation
    atari_opt_no_dos = 1,
    // Flag for PAL or NTSC timings
    atari_opt_pal = 2,
    // Flag for real-time or cycle based time
    atari_opt_cycletime = 4,
    // Flag for using the original Atari math pack
    atari_opt_atari_mathpack = 8,
    // Flag for using the simple E: stream mode (no editor emulation)
    atari_opt_no_editor = 16
};

typedef struct
{
    // Callback for character input to the simulator
    int (*get_char)(void);
    // Callback for character output from the simulator
    void (*put_char)(int);
    // Emulation flags
    int flags;
} emu_options;

// Parse option flags
int atari_add_option(emu_options *opt, const char *str);
// Init bios callbacks, with given options.
void atari_init(sim65 s, emu_options *opts);
// Load (and RUN) XEX file
enum sim65_error atari_xex_load(sim65 s, const char *name, int check);
// Load ROM file
enum sim65_error atari_rom_load(sim65 s, int addr, const char *name);
// Boot from a loaded disk image
enum sim65_error atari_boot_image(sim65 s);
// Install a callback handler, with an RTS in rom
void add_rts_callback(sim65 s, unsigned addr, unsigned len, sim65_callback cb);
// Load a disk image
int atari_load_image(sim65 s, const char *file_name);
// Adds command line parameters to emulated DOS
void atari_dos_add_cmdline(sim65 s, const char *cmd);
// Sets a base path for all file access in emulated DOS
void atari_dos_set_root(sim65 s, const char *path);
// Get current emulation flags
int atari_get_flags(sim65 s);
// Current value of the keyboard register CH ($2FC), $FF if none pending.
// Reads the value stored by the emulation or an explicit program write,
// without consuming any character from the input stream.
int atario_ch_value(sim65 s);
// Clears the pending keyboard code, without consuming characters from
// the input stream.
void atario_ch_clear(sim65 s);
// Read a character from the emulated screen buffer (row 0..23, col 0..39).
unsigned char atari_screen_get(int row, int col);
// Get the current E: cursor position (row 0..23, col 0..39).
void atario_cursor_get(int *row, int *col);

// Full editor mode flag (default on; disable with -o noeditor)
extern int atari_opt_full_editor;
