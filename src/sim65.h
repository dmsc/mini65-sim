/*
 * Mini65 - Small 6502 simulator with Atari 8bit bios.
 * Copyright (C) 2017,2018 Daniel Serpell
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

typedef struct sim65s *sim65;

/// Creates new simulator state, with no address regions defined.
sim65 sim65_new();
/// Adds an uninitialized RAM region.
void sim65_add_ram(sim65 s, unsigned addr, unsigned len);
/// Adds a zeroed RAM region.
void sim65_add_zeroed_ram(sim65 s, unsigned addr, unsigned len);
/// Adds a RAM region with the given data.
void sim65_add_data_ram(sim65 s, unsigned addr, const unsigned char *data, unsigned len);
/// Adds a ROM region with the given data.
void sim65_add_data_rom(sim65 s, unsigned addr, const unsigned char *data, unsigned len);
/// Sets debug flag
void sim65_set_debug(sim65 s, unsigned level);

/// Struct used to pass the register values
struct sim65_reg
{
    unsigned pc;
    unsigned char a, x, y, p, s;
};

enum sim65_cb_type
{
    sim65_cb_write = 0,
    sim65_cb_read = -1,
    sim65_cb_exec = -2
};

/** Callback from the simulator.
 * @param s sim65 state.
 * @param regs simulator register values before the instruction.
 * @param addr address of memory causing the callback.
 * @param data type of callback:
 *             sim65_cb_read = read memory
 *             sim65_cb_exec = execute address
 *             other value   = write memory, data is the value to write.
 * @returns <0 on error, value to read in case of read-callback. */
typedef int (*sim65_callback)(sim65 s, struct sim65_reg *regs, unsigned addr, int data);

/// Adds a callback at the given address of the given type
void sim65_add_callback(sim65 s, unsigned addr, sim65_callback cb, enum sim65_cb_type type);
/// Adds a callback at the given address range of the given type
void sim65_add_callback_range(sim65 s, unsigned addr, unsigned len,
                              sim65_callback cb, enum sim65_cb_type type);

/// Reads from simulation state.
unsigned sim65_get_byte(sim65 s, unsigned addr);

/// Runs the simulation. Stops at BRK, a callback returning != 0 or execution errors.
/// If regs is NULL, initializes the registers to zero.
int sim65_run(sim65 s, struct sim65_reg *regs, unsigned addr);

/// Calls the simulation.
/// Simulates a call via a JSR to the given address, pushing a (fake) return address
/// to the stack and returning on the matching RTS.
/// If regs is NULL, initializes the registers to zero.
/// @returns 0 if exit through the RTS, non 0 when stops at BRK, a callback
///          returning != 0 or execution errors.
int sim65_call(sim65 s, struct sim65_reg *regs, unsigned addr);

/// Prints the current register values
void sim65_print_reg(sim65 s);
