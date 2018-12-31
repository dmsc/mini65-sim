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
#include <string.h>
#include <unistd.h>

static void print_help(const char *name)
{
    fprintf(stderr, "Usage: %s [options] <filename>\n"
                    "Options:\n"
                    " -h: Show this help\n"
                    " -d: Print debug messages\n"
                    " -t: Print simulation trace\n"
                    " -e <lvl>: Sets the error level to 'none', 'mem' or 'full'\n"
                    " -l <file>: Loads label file, used in simulation trace\n"
                    " -r <addr>: Loads rom at give address instead of XEX file\n",
            name);
}

static void print_error(const char *text, const char *name)
{
    if (text)
        fprintf(stderr, "%s: %s\n", name, text);
    fprintf(stderr,"%s: Try '-h' for help.\n", name);
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
    int opt;
    unsigned rom = 0;
    const char *lblname = 0;

    s = sim65_new();
    if (!s)
        exit_error("internal error", argv[0]);

    while ((opt = getopt(argc, argv, "tdhr:l:e:")) != -1)
    {
        switch (opt)
        {
            case 't': // trace
                sim65_set_debug(s, sim65_debug_trace);
                break;
            case 'd': // debug
                sim65_set_debug(s, sim65_debug_messages);
                break;
            case 'e': // error level
                if (!strcmp(optarg, "n") || !strcmp(optarg, "none"))
                    sim65_set_error_level(s, sim65_errlvl_none);
                else if (!strcmp(optarg, "f") || !strcmp(optarg, "full"))
                    sim65_set_error_level(s, sim65_errlvl_full);
                else if (!strcmp(optarg, "m") || !strcmp(optarg, "mem"))
                    sim65_set_error_level(s, sim65_errlvl_memory);
                else
                    print_error("invalid error level", argv[0]);
                break;
            case 'h': // help
                print_help(argv[0]);
                return 0;
            case 'r': // rom address
                rom = strtol(optarg, 0, 0);
                break;
            case 'l': // label file
                lblname = optarg;
                break;
            default:
                print_error(0, argv[0]);
        }
    }

    if (optind >= argc)
        print_error("missing filename", argv[0]);
    else if (optind + 1 != argc)
        print_error("only one filename allowed", argv[0]);
    const char *fname = argv[optind];

    // Load labels file
    if (lblname)
        sim65_lbl_load(s, lblname);

    // Initialize Atari emu
    atari_init(s, lblname != 0);

    // Read and execute file
    enum sim65_error e;
    if (rom)
    {
        int l = atari_rom_load(s, rom, fname);
        if (l < 0)
            exit_error("error reading ROM file", argv[0]);
        e = sim65_call(s, 0, rom);
    }
    else
    {
        e = atari_xex_load(s, fname);
        if (e == sim65_err_user)
            exit_error("error reading binary file", argv[0]);
    }
    // start
    if (e)
        // Prints error message
        sim65_eprintf(s, "simulator returned %s at address %04x.",
                      sim65_error_str(s, e), sim65_error_addr(s));
    return 0;
}
