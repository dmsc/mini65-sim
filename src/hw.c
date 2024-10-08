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
#include "hw.h"
#include "atari.h"
#include "sim65.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

static int sim_exec_error(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    sim65_eprintf(s, "invalid exec address $%04x", addr);
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
                sim65_dprintf(s, "GTIA read $%04x", addr);
                return 0;
        }
    }
    else
        sim65_dprintf(s, "GTIA write $%04x <- $%02x", addr, data);
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
        sim65_dprintf(s, "POKEY read $%04x", addr);
    }
    else
    {
        // Don't log zero writes
        if (data != 0)
            sim65_dprintf(s, "POKEY write $%04x <- $%02x", addr, data);
    }
    return 0;
}

static int sim_pia(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    int reg = addr & 0x03;
    // Implement 128K memory (Atari 130XE):
    if (reg == 0x01) // PORTB
    {
        // This does not emulate PBCTL, assuming the direction bits are
        // correctly setup.
        static uint8_t state = 0xFF;
        if (data == sim65_cb_read)
            return state;
        else
        {
            if (!(data & 0x1))
                sim65_dprintf(s, "PIA: ROM banking not implemented");
            int pre_bank = (state & 0x10) ? 1 : 4 + ((state >> 2) & 3);
            int new_bank = (data & 0x10) ? 1 : 4 + ((data >> 2) & 3);
            if (pre_bank != new_bank)
            {
                sim65_dprintf(s, "PIA: setting bank %d from %d", new_bank, pre_bank);
                // Swap out old bank, swap in new bank
                sim65_swap_bank(s, 0x4000, pre_bank * 0x4000, 0x4000);
                sim65_swap_bank(s, 0x4000, new_bank * 0x4000, 0x4000);
            }
            state = data;
            return 0;
        }
    }
    if (data == sim65_cb_read)
        sim65_dprintf(s, "PIA read $%04x", addr);
    else
        sim65_dprintf(s, "PIA write $%04x <- $%02x", addr, data);
    return 0;
}

// Get the vcount counter
static int64_t atari_hw_vcount(sim65 s, int flags)
{
    if (flags & atari_opt_cycletime)
    {
        // Each scan line has 114 cycles, with 9 cycles for refresh, so we
        // simulate 105 cycles/frame.
        int64_t scanlines = sim65_get_cycles(s) / 105;

        // In NTSC, there are 262 scan lines per frame, in PAL there are
        // 312 scan lines per frame, the scan line counter only counts even
        // lines.
        return scanlines / 2;
    }
    else
    {
        // Get current time in seconds:
        struct timeval tv;
        gettimeofday(&tv, 0);
        double time = (tv.tv_sec + tv.tv_usec * 0.000001);
        // Scale depending on PAL/NTSC setting
        if (flags & atari_opt_pal)
            time = time * (15556.55 * 0.5); // PAL
        else
            time = time * (15699.75 * 0.5); // NTSC
        if (sizeof(long) == sizeof(int64_t))
            return lrint(time);
        else
            return llrint(time);
    }
}

// Get the frame counter
int64_t atari_hw_framenum(sim65 s)
{
    int flags      = atari_get_flags(s);
    int64_t vcount = atari_hw_vcount(s, flags);

    if (flags & atari_opt_pal)
        return vcount / 156;
    else
        return vcount / 131;
}

static int sim_antic(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    // addr & 0x0F
    if (data == sim65_cb_read)
    {
        sim65_dprintf(s, "ANTIC read $%04x", addr);
        if ((addr & 0xFF) == 0x0B)
        {
            int flags      = atari_get_flags(s);
            int64_t vcount = atari_hw_vcount(s, flags);
            if (flags & atari_opt_pal)
                return vcount % 156;
            else
                return vcount % 131;
        }
    }
    else
        sim65_dprintf(s, "ANTIC write $%04x <- $%02x", addr, data);
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
