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
#include "atari.h"
#include "sim65.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

    // Initialize Atari emu
    atari_init(s);

    // Read file
    if (rom)
    {
        int l = atari_rom_load(s, rom, fname);
        if (l < 0)
            exit_error("error reading ROM file", argv[0]);
        start = rom;
    }
    else
    {
        start = atari_xex_load(s, fname);
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
