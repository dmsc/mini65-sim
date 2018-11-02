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
#include "hw.h"
#include "sim65.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int sim_exec_error(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    fprintf(stderr, "Invalid exec address $%04x\n", addr);
    return -1;
}

static int sim_gtia(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    // addr & 0x1F
    if (data == sim65_cb_read)
    {
        switch (addr - 0xD000)
        {
            case 0x1f:
                return 7; // CONSOL, no key pressed.
            default:
                fprintf(stderr, "Read GTIA $%04x\n", addr);
                return 0;
        }
    }
    else
        fprintf(stderr, "Write GTIA $%04x <- $%02x\n", addr, data);
    return 0;
}

static int rand32()
{
    static uint32_t a, b, c, d, seed;
    if (!seed)
    {
        a = 0xf1ea5eed, b = c = d = seed = 123;
    }
    uint32_t e;
    e = a - ((b << 27) | (b >> 5));
    a = b ^ ((c << 17) | (c >> 15));
    b = c + d;
    c = d + e;
    d = e + a;
    return d;
}

static int sim_pokey(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    // addr & 0x0F
    if (data == sim65_cb_read)
    {
        if (addr == 0xD20A)
        {
            return 0xFF & rand32();
        }
        fprintf(stderr, "Read POKEY $%04x\n", addr);
    }
    else
    {
        // Don't log zero writes
        if( data != 0 )
            fprintf(stderr, "Write POKEY $%04x <- $%02x\n", addr, data);
    }
    return 0;
}

static int sim_pia(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    // addr & 0x03
    if (data == sim65_cb_read)
        fprintf(stderr, "Read PIA $%04x\n", addr);
    else
        fprintf(stderr, "Write PIA $%04x <- $%02x\n", addr, data);
    return 0;
}

static int sim_antic(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    // addr & 0x0F
    if (data == sim65_cb_read)
        fprintf(stderr, "Read ANTIC $%04x\n", addr);
    else
        fprintf(stderr, "Write ANTIC $%04x <- $%02x\n", addr, data);
    return 0;
}

void atari_hardware_init(sim65 s)
{
    // HW registers
    sim65_add_callback_range(s, 0xD000, 0x100, sim_gtia, sim65_cb_read);
    sim65_add_callback_range(s, 0xD200, 0x100, sim_pokey, sim65_cb_read);
    sim65_add_callback_range(s, 0xD300, 0x100, sim_pia, sim65_cb_read);
    sim65_add_callback_range(s, 0xD400, 0x100, sim_antic, sim65_cb_read);
    sim65_add_callback_range(s, 0xD000, 0x100, sim_gtia, sim65_cb_write);
    sim65_add_callback_range(s, 0xD200, 0x100, sim_pokey, sim65_cb_write);
    sim65_add_callback_range(s, 0xD300, 0x100, sim_pia, sim65_cb_write);
    sim65_add_callback_range(s, 0xD400, 0x100, sim_antic, sim65_cb_write);
    // Error out on EXEC to HW range
    sim65_add_callback_range(s, 0xD000, 0x7FF, sim_exec_error, sim65_cb_exec);
}
