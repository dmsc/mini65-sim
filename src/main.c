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
#include "atari.h"
#include "sim65.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>

static char *prog_name;
static FILE *trace_file;

static void print_help(void)
{
    fprintf(stderr, "Usage: %s [options] <filename> [command line]\n"
                    "Options:\n"
                    " -h: Show this help\n"
                    " -d: Print debug messages to standard error\n"
                    " -b: Use binary for standard input and output, the default is to\n"
                    "     translate ATASCII to ASCII and vice-versa\n"
                    " -D: Don´t emulate DOS device.\n"
                    " -R <path>: Set a root path for emulated DOS device.\n"
                    " -I <file>: Loads a disk image for SIO emulation.\n"
                    "            If no executable is given, boots from this image.\n"
                    " -e <lvl> : Sets the error level to 'none', 'mem' or 'full'\n"
                    " -t <file>: Store simulation trace into file\n"
                    " -l <file>: Loads label file, used in simulation trace. With multiple\n"
                    "            label files loaded, last one takes precedence.\n"
                    " -r <addr>: Loads rom at give address instead of XEX file\n"
                    " -p <file>: Store profile information into file\n"
                    " -P <file>: Read/write binary profile data to file, use to consolidate\n"
                    "            more than one profile run\n",
            prog_name);
}

static void print_error(const char *text)
{
    if (text)
        fprintf(stderr, "%s: %s\n", prog_name, text);
    fprintf(stderr,"%s: Try '-h' for help.\n", prog_name);
    exit(1);
}

static void exit_error(const char *text)
{
    fprintf(stderr, "%s: %s.\n", prog_name, text);
    exit(1);
}

static void store_prof(const char *fname, sim65 s)
{
    FILE *f = fopen(fname, "w");
    if (!f)
    {
        perror(fname);
        exit_error("can't open profile.");
    }
    struct sim65_profile pdata = sim65_get_profile_info(s);
    uint64_t max_count = 1000;
    for (unsigned i=0; i<pdata.max; i++)
        if (pdata.cycle_count[i] > max_count)
            max_count = pdata.cycle_count[i];
    int digits = 0;
    while( max_count )
    {
        digits ++;
        max_count /= 10;
    }
    char buf[256];
    for (unsigned i=0; i<pdata.max; i++)
        if (pdata.cycle_count[i])
        {
            fprintf(f, "%*" PRIu64 " %04X %s", digits, pdata.cycle_count[i], i,
                    sim65_disassemble(s, buf, i));
            if (pdata.branch_taken[i])
            {
                // Calculate number of cycles spent on taken branches:
                uint64_t cyc = pdata.branch_taken[i] * 3 + pdata.extra_cycles[i];
                if (pdata.cycle_count[i] == cyc)
                    fprintf(f, " (always taken");
                else
                    fprintf(f, " (%" PRIu64 " times taken", pdata.branch_taken[i]);
                fprintf(f, "%s", pdata.extra_cycles[i] ? ", crosses page)" : ")");
            }
            else if(sim65_ins_is_branch(s, i))
                fprintf(f, " (never taken)");
            else if(pdata.extra_cycles[i])
                fprintf(f, " (%" PRIu64 " times crossed pages)", pdata.extra_cycles[i]);
            if (pdata.cycle_count[i] <= pdata.flag_change[i])
                fprintf(f, " (no useful work)");
            fputc('\n', f);
        }
    // Summary at end
    uint64_t ti  = pdata.total.instructions;
    uint64_t tb  = pdata.total.branch_skip + pdata.total.branch_taken;
    fprintf(f, "--------- Total Instructions:    %10" PRIu64 "\n"
               "--------- Total Branches:        %10" PRIu64 " (%.1f%% of instructions)\n"
               "--------- Total Branches Taken:  %10" PRIu64 " (%.1f%% of branches)\n"
               "--------- Branches cross-page:   %10" PRIu64 " (%.1f%% of taken branches)\n"
               "--------- Absolute X cross-page: %10" PRIu64 "\n"
               "--------- Absolute Y cross-page: %10" PRIu64 "\n"
               "--------- Indirect Y cross-page: %10" PRIu64 "\n",
               ti, tb, 100.0 * tb / ti,
               pdata.total.branch_taken, 100.0 * pdata.total.branch_taken / tb,
               pdata.total.branch_extra, 100.0 * pdata.total.branch_extra / pdata.total.branch_taken,
               pdata.total.extra_abs_x, pdata.total.extra_abs_y, pdata.total.extra_ind_y );

    fclose(f);
}

static void set_trace_file(const char *fname, sim65 s)
{
    trace_file = fopen(fname, "w");
    if (!trace_file)
    {
        perror(fname);
        exit_error("can't open trace file.");
    }
    sim65_set_trace_file(s, trace_file);
}

// Raw put/get character, used on "untranslated" mode
static int raw_get_char(void)
{
    return getchar();
}

static void raw_put_char(int c)
{
    putchar(c);
}

static sim65 handle_sigint_s;
static void handle_sigint(int sig)
{
    // Set's cycle limit
    sim65_set_cycle_limit(handle_sigint_s, 1);
}

int main(int argc, char **argv)
{
    sim65 s;
    int opt;
    unsigned rom = 0;
    const char *profname = 0, *profdata = 0, *load_img = 0;
    const char *rootpath = 0;
    emu_options opts     = { .get_char = 0, .put_char = 0, .emu_dos = 1 };

    prog_name = argv[0];
    s = sim65_new();
    if (!s)
        exit_error("internal error");

    while ((opt = getopt(argc, argv, "t:dbhr:l:e:p:P:I:DR:")) != -1)
    {
        switch (opt)
        {
            case 't': // trace
                sim65_set_debug(s, sim65_debug_trace);
                set_trace_file(optarg, s);
                break;
            case 'd': // debug
                sim65_set_debug(s, sim65_debug_messages);
                break;
            case 'D': // no dos
                opts.emu_dos = 0;
                break;
            case 'I': // load image
                load_img = optarg;
                break;
            case 'b': // binary/raw
                opts.get_char = raw_get_char;
                opts.put_char = raw_put_char;
                break;
            case 'e': // error level
                if (!strcmp(optarg, "n") || !strcmp(optarg, "none"))
                    sim65_set_error_level(s, sim65_errlvl_none);
                else if (!strcmp(optarg, "f") || !strcmp(optarg, "full"))
                    sim65_set_error_level(s, sim65_errlvl_full);
                else if (!strcmp(optarg, "m") || !strcmp(optarg, "mem"))
                    sim65_set_error_level(s, sim65_errlvl_memory);
                else
                    print_error("invalid error level");
                break;
            case 'h': // help
                print_help();
                return 0;
            case 'r': // rom address
                rom = strtol(optarg, 0, 0);
                break;
            case 'l': // label file
                sim65_lbl_load(s, optarg);
                break;
            case 'p': // profile
                profname = optarg;
                break;
            case 'P': // profile data
                profdata = optarg;
                break;
            case 'R': // root path
                rootpath = optarg;
                break;
            default:
                print_error(0);
        }
    }

    const char *fname = 0;
    if (optind + 1 < argc && !opts.emu_dos)
        print_error("only one filename allowed");
    else if (optind < argc)
        fname = argv[optind];
    else if (!load_img)
        print_error("missing filename");

    // Initialize Atari emu
    atari_init(s, &opts);

    if (rootpath)
    {
        if (!opts.emu_dos)
            print_error("root path is only valid for emulated DOS");
        atari_dos_set_root(s, rootpath);
    }

    // Add command line
    if (opts.emu_dos && fname && optind + 1 < argc)
    {
        atari_dos_add_cmdline(s, fname);
        for (int i = optind + 1; i < argc; i++)
            atari_dos_add_cmdline(s, argv[i]);
    }

    // Load disk image
    if (load_img)
        if( atari_load_image(s, load_img) )
            exit_error("can't load disk image");

    // Set profile info
    if (profname || profdata)
        sim65_set_profiling(s, 1);

    if (profdata)
    {
        if (sim65_load_profile_data(s, profdata))
            return 1;
    }

    // Adds a signal handler for CONTROL-C, so we exit from the
    // simulator cleanly
    handle_sigint_s = s;
    if( SIG_ERR == signal(SIGINT, handle_sigint) )
        sim65_dprintf(s, "Error setting signal handler.");

    // Read and execute file
    enum sim65_error e;
    if (rom)
    {
        e = atari_rom_load(s, rom, fname);
        if (e == sim65_err_user)
            exit_error("error reading ROM file");
    }
    else if (fname)
    {
        // Load file without checking
        e = atari_xex_load(s, fname, 0);
        if (e == sim65_err_user)
            exit_error("error reading binary file");
    }
    else
    {
        e = atari_boot_image(s);
        if (e == sim65_err_user)
            exit_error("error booting from disk image");
    }
    // start
    if (e == sim65_err_cycle_limit)
        sim65_eprintf(s, "stopped at address $%04x.", sim65_error_addr(s));
    else if (e)
        // Prints error message
        sim65_eprintf(s, "%s at address $%04x.",
                      sim65_error_str(s, e), sim65_error_addr(s));
    sim65_dprintf(s, "Total cycles: %ld", sim65_get_cycles(s));

    if (profdata)
        sim65_save_profile_data(s, profdata);
    if (profname)
        store_prof(profname, s);
    sim65_free(s);
    if (trace_file)
        fclose(trace_file);
    return 0;
}
