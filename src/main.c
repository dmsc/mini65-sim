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
#include "abios.h"
#include "sim65.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int read_rom_file(sim65 s, int addr, const char *name)
{
    int c, saddr = addr;
    FILE *f = fopen(name, "rb");
    if (!f)
        return -1;

    while (EOF != (c = getc(f)))
    {
        unsigned char data = c;
        sim65_add_data_rom(s, addr++, &data, 1);
    }
    return addr - saddr;
}

static int read_xex_file(sim65 s, const char *name)
{
    int state = 0, saddr = 0, eaddr = 0, start = 0, loadad = 0, runad = 0;
    FILE *f = fopen(name, "rb");
    if (!f)
        return -1;
    do
    {
        unsigned char data;
        int c = getc(f);
        if (c == EOF)
        {
            if (start)
                return start;
            else if (runad)
                return runad;
            else
                return loadad;
        }
        switch (state)
        {
            case 0:
            case 1:
                if (c != 0xFF)
                    return -1;
                break;
            case 2:
                saddr = c;
                break;
            case 3:
                loadad = saddr |= c << 8;
                if (!runad)
                    runad = loadad;
                break;
            case 4:
                eaddr = c;
                break;
            case 5:
                eaddr |= c << 8;
                break;
            case 6:
                data = c;
                sim65_add_data_ram(s, saddr, &data, 1);
                if (saddr == 0x02E0)
                    start = (start & 0xFF00) | data;
                else if (saddr == 0x02E1)
                    start = (start & 0xFF) | (data << 8);
                if (saddr == 0x02E2)
                    runad = (runad & 0xFF00) | data;
                else if (saddr == 0x02E3)
                    runad = (runad & 0xFF) | (data << 8);
                if (saddr != eaddr)
                {
                    saddr++;
                    continue;
                }
                break;
            case 7:
                saddr = c;
                break;
            case 8:
                saddr |= c << 8;
                if (saddr == 0xFFFF)
                    state = 2;
                else
                    state = 4;
                continue;
        }
        state++;
    } while (1);
}

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

static void init_atari_hardware(sim65 s)
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

static void print_help(const char *name)
{
    fprintf(stderr, "Usage: %s [options] <filename>\n"
                    "Options:\n"
                    " -h: Show this help\n"
                    " -t: Print simulation trace\n"
                    " -r <addr>: Loads rom at give address instead of XEX file\n",
            name);
}

static void print_error(const char *text, const char *name)
{
    fprintf(stderr, "%s: %s, try '-h' for help.\n", name, text);
    exit(1);
}

static void exit_error(const char *text, const char *name)
{
    fprintf(stderr, "%s: %s.\n", name, text);
    exit(1);
}

int main(int argc, char **argv)
{
    sim65 s;
    int start, e, i;
    char *fname       = 0;
    unsigned dbgLevel = 0;
    unsigned rom      = 0;
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            if (argv[i][1] == 't') // trace
                dbgLevel = 1;
            else if (argv[i][1] == 'h') // help
            {
                print_help(argv[0]);
                return 0;
            }
            else if (argv[i][1] == 'r') // load rom
            {
                if (i + 1 == argc)
                    print_error("option -r expects argument (rom address)", argv[0]);
                i++;
                rom = strtol(argv[i], 0, 0);
            }
            else
                print_error("invalid option", argv[0]);
        }
        else if (!fname)
            fname = argv[i];
        else
            print_error("only one filename allowed", argv[0]);
    }
    if (!fname)
        print_error("missing filename", argv[0]);

    s = sim65_new();
    if (!s)
        exit_error("internal error", argv[0]);
    // Set debug level
    sim65_set_debug(s, dbgLevel);
    // Add 64k of uninitialized ram
    sim65_add_ram(s, 0, 0x10000);
    // Needed for atari emu
    init_atari_hardware(s);
    abios_init(s);

    // Read file
    if (rom)
    {
        int l = read_rom_file(s, rom, fname);
        if (l < 0)
            exit_error("error reading ROM file", argv[0]);
        start = rom;
    }
    else
    {
        start = read_xex_file(s, fname);
        if (start < 0)
            exit_error("error reading binary file", argv[0]);
        if (!start)
            exit_error("missing start address", argv[0]);
    }
    // start
    e = sim65_call(s, 0, start);
    sim65_print_reg(s);
    printf("Simulation returns: %d\n", e);
    return 0;
}
