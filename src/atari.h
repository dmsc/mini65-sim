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

// Init bios callbacks, optionally passing a callback to print a
// character and read a character.
void atari_init(sim65 s, int (*get_char)(void),
                void (*put_char)(int), int emu_dos);
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
