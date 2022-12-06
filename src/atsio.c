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

/* Implements Atari SIO emulation */
#include "atsio.h"
#include "atari.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// SIO defs
#define SIOV   (0xE459) // Vector to SIO
#define DDEVIC (0x0300) // PERIPHERAL UNIT 1 BUS I.D. NUMBER
#define DUNIT  (0x0301) // UNIT NUMBER
#define DCOMND (0x0302) // BUS COMMAND
#define DSTATS (0x0303) // COMMAND TYPE/STATUS RETURN
#define DBUFLO (0x0304) // 1-byte low data buffer address
#define DBUFHI (0x0305) // 1-byte high data buffer address
#define DTIMLO (0x0306) // DEVICE TIME OUT IN 1 SECOND UNITS
#define DUNUSE (0x0307) // UNUSED BYTE
#define DBYTLO (0x0308) // 1-byte low number of bytes to transfer
#define DBYTHI (0x0309) // 1-byte high number of bytes to transfer
#define DAUX1  (0x030A) // 1-byte first command auxiliary
#define DAUX2  (0x030B) // 1-byte second command auxiliary

// Utility functions
static unsigned peek(sim65 s, unsigned addr)
{
    return sim65_get_byte(s, addr);
}

static unsigned dpeek(sim65 s, unsigned addr)
{
    return sim65_get_byte(s, addr) + (sim65_get_byte(s, addr + 1) << 8);
}

static int sim_SIOV(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    // Read DCB
    unsigned ddevic = peek(s, 0x0300);
    unsigned dunit  = peek(s, 0x0301);
    unsigned dcomnd = peek(s, 0x0302);
    unsigned dstats = peek(s, 0x0303);
    unsigned dbuf   = dpeek(s, 0x0304);
    unsigned dtimlo = peek(s, 0x0306);
    unsigned dbyt   = dpeek(s, 0x0308);
    unsigned daux1  = peek(s, 0x030A);
    unsigned daux2  = peek(s, 0x030B);

    sim65_dprintf(s, "SIO call"
            " dev=%02x:%02x cmd=%02x stat=%02x buf=%04x tim=%02x len=%04x aux=%02x:%02x",
            ddevic, dunit, dcomnd, dstats, dbuf, dtimlo, dbyt, daux1, daux2);
    sim65_set_flags(s, SIM65_FLAG_N, SIM65_FLAG_N);
    regs->y = 0x8A;
    return 0;
}

void atari_sio_init(sim65 s)
{
    // CIOV
    add_rts_callback(s, SIOV, 1, sim_SIOV);
    // Mark DCB as uninitialized
    sim65_add_ram(s, DDEVIC, 11);
}

