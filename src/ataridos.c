/*
 * Mini65 - Small 6502 simulator with Atari 8bit bios.
 * Copyright (C) 2017-2024 Daniel Serpell
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

/* Implements BW-DOS (or Sparta DOS 3.x) emulation */
#include "ataridos.h"
#include "atari.h"
#include "atcio.h"
#include "ciodev.h"
#include "dosfname.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *root_path = ".";

// Misc routines
static unsigned peek(sim65 s, unsigned addr)
{
    return sim65_get_byte(s, addr);
}

static unsigned dpeek(sim65 s, unsigned addr)
{
    return sim65_get_byte(s, addr) + (sim65_get_byte(s, addr + 1) << 8);
}

static void poke(sim65 s, unsigned addr, unsigned char val)
{
    sim65_add_data_ram(s, addr, &val, 1);
}

static void dpoke(sim65 s, unsigned addr, unsigned val)
{
    poke(s, addr, val & 0xFF);
    poke(s, addr + 1, val >> 8);
}

// Handlers for COMTAB jumps
#define DOS_CP     (DOSCP_BASE + 0)
#define DOS_CRNAME (DOSCP_BASE + 1)
#define DOS_DIVIO  (DOSCP_BASE + 2)
#define DOS_XDIVIO (DOSCP_BASE + 3)
#define DOS_LSIO   (DOSCP_BASE + 4)
#define DOS_CONVDC (DOSCP_BASE + 5)
// COMTAB variables and buffers
#define DOS_BUFOFF (COMTAB_BASE + 10)
#define DOS_COMFNM (COMTAB_BASE + 33)
#define DOS_LBUF   (COMTAB_BASE + 63)

// Emulated DOS handler
static const unsigned char devhand_emudos[] = {
    DEVH_TAB(DISKD)
};

static void sim_DOS_CRNAME(sim65 s, struct sim65_reg *regs)
{
    // Copy from LBUF + BUFOFF into COMFNM, starting with 'D1:'
    // Build the command line in a buffer
    char buf[256] = { 'D', '1', ':' };
    int off = peek(s, DOS_BUFOFF), dev = 0, arg = 0, len;

    for (len = 3; off < 64 && len < 27; off++)
    {
        int c = peek(s, DOS_LBUF + off);
        if (c == 0x9B)
            break;
        if (c == ' ' && !arg)
            continue;
        if (c == ' ' || c == 0x9B)
            break;
        arg = 1;
        if (c == ':' && !dev)
        {
            // Move back, overwriting the original "D1:"
            dev = 1;
            len = len - 3;
            for (int i = 0; i < len; i++)
                buf[i] = buf[i + 3];
        }
        // Ok, copy character
        buf[len++] = c;
    }
    buf[len] = 0;
    sim65_dprintf(s, "DOS CRNAME: '%s'", buf);
    // Store into 6502 memory
    poke(s, DOS_BUFOFF, off);
    for (int i = 0; i < len; i++)
        poke(s, DOS_COMFNM + i, buf[i]);
    while (len < 28)
        poke(s, DOS_COMFNM + len++, 0x9B);
}

static int sim_DOS_COMTAB(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    switch (addr & 7)
    {
        case 0:
            // Exit to DOS
            sim65_dprintf(s, "DOS CP: exit");
            return sim65_err_call_ret;
        case 1:
            // CRNAME, get name from command line
            sim_DOS_CRNAME(s, regs);
            return 0;
        case 2:
            // DIVIO, start hard copy or batch file
            sim65_dprintf(s, "DOS DIVIO");
            return 0;
        case 3:
            // XDIVIO, end hard copy or batch file
            sim65_dprintf(s, "DOS XDIVIO");
            return 0;
        case 4:
            // LSIO, call SIO
            sim65_dprintf(s, "DOS LSIO");
            regs->pc = 0xE459;
            return 0;
        case 5:
            // CONVDC
            sim65_dprintf(s, "DOS CONVDC");
            return 0;
        default:
            sim65_dprintf(s, "invalid DOS call");
            return 0;
    }
}

static int sim_DISKD(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    // Store one file handle for each CIO channel
    static FILE *fhand[16];

    // We need IOCB data
    unsigned chn  = (regs->x >> 4);
    unsigned cmd  = peek(s, ICCOMZ);
    unsigned badr = dpeek(s, ICBALZ);
    unsigned dno  = peek(s, ICDNOZ);
    unsigned ax1  = peek(s, ICAX1Z);
    unsigned ax2  = peek(s, ICAX2Z);

    switch (addr & 7)
    {
        case DEVR_OPEN:
        {
            // Decode file name
            char fname[256];
            int i;
            // Skip 'D#:'
            badr++;
            if (dno == (peek(s, badr) - '0'))
                badr++;
            if (peek(s, badr) == ':')
                badr++;
            // Translate rest of filename
            for (i = 0; i < 250; i++)
            {
                int c = peek(s, badr++);
                if (!c || c == 0x9b)
                    break;
                fname[i] = c;
            }
            fname[i] = 0;
            sim65_dprintf(s, "DISK OPEN #%d, %d, %d, '%s'", chn, ax1, ax2, fname);
            // Test if not already open
            if (fhand[chn])
            {
                sim65_dprintf(s, "DISK: Internal error, %d already open.", chn);
                fclose(fhand[chn]);
                fhand[chn] = 0;
            }
            // Open Flag:
            const char *flags = 0;
            switch (ax1)
            {
                case 4: // Open for read
                    flags = "rb";
                    break;
                case 8: // Open for write
                    flags = "wb";
                    break;
                case 9: // Open for append
                    flags = "ab";
                    break;
                case 12: // Open for update
                    flags = "r+b";
                    break;
                case 6:
                    // TODO: Directory read
                default:
                    regs->y = 0xA8;
                    return 0;
            }
            fhand[chn] = dosfopen(root_path, fname, flags);
            if (!fhand[chn])
            {
                sim65_dprintf(s, "DISK OPEN: error %s", strerror(errno));
                if (errno == ENOENT)
                    regs->y = 170;
                else if (errno == ENOSPC)
                    regs->y = 162;
                else if (errno == EACCES)
                    regs->y = 167;
                else
                    regs->y = 139;
            }
            else
                regs->y = 1;
            return 0;
        }
        case DEVR_CLOSE:
            if (fhand[chn])
            {
                fclose(fhand[chn]);
                fhand[chn] = 0;
            }
            regs->y = 1;
            return 0;
        case DEVR_GET:
            if (!fhand[chn])
            {
                sim65_dprintf(s, "DISK GET: Internal error, %d closed.", chn);
                regs->y = 133;
            }
            else
            {
                int c   = fgetc(fhand[chn]);
                regs->y = 1;
                if (c == EOF)
                    regs->y = 136;
                else
                    regs->a = c;
            }
            return 0;
        case DEVR_PUT:
            if (!fhand[chn])
            {
                sim65_dprintf(s, "DISK PUT: Internal error, %d closed.", chn);
                regs->y = 133;
            }
            else
            {
                fputc(regs->a, fhand[chn]);
                regs->y = 1;
            }
            return 0;
        case DEVR_STATUS:
            return 0;
        case DEVR_SPECIAL:
            if (cmd == 37)
            {
                unsigned ax3 = peek(s, regs->x + ICAX3);
                unsigned ax4 = peek(s, regs->x + ICAX4);
                unsigned ax5 = peek(s, regs->x + ICAX5);
                if (!fhand[chn])
                {
                    sim65_dprintf(s, "DISK POINT: %d closed.", chn);
                    regs->y = 133;
                }
                else
                {
                    sim65_dprintf(s, "DISK POINT: $%02x.%02x.%02x", ax5, ax4, ax3);
                    long offset = (ax5 << 16) | (ax4 << 8) | ax3;
                    fseek(fhand[chn], offset, SEEK_SET);
                    regs->y = 1;
                }
            }
            else if (cmd == 38)
            {
                if (!fhand[chn])
                {
                    sim65_dprintf(s, "DISK NOTE: %d closed.", chn);
                    regs->y = 133;
                }
                else
                {
                    long offset = ftell(fhand[chn]);
                    regs->y = 1;
                    unsigned ax3 = offset & 0xFF;
                    unsigned ax4 = (offset >> 8) & 0xFF;
                    unsigned ax5 = (offset >> 16) & 0xFF;
                    poke(s, regs->x + ICAX3, ax3);
                    poke(s, regs->x + ICAX4, ax4);
                    poke(s, regs->x + ICAX5, ax5);
                    sim65_dprintf(s, "DISK NOTE = $%02x.%02x.%02x", ax5, ax4, ax3);
                }
            }
            return 0;
        case DEVR_INIT:
            return 0;
        default:
            sim65_eprintf(s, "invalid access to cb address $%04x", addr);
            return 0;
    }
}

void atari_dos_add_cmdline(sim65 s, const char *cmd)
{
    if (!cmd)
        return;

    // Search current command line length
    int len = 0;
    for (int i = 0; i < 64; i++)
        if (peek(s, DOS_LBUF + i) == 0x9B)
        {
            len = i;
            break;
        }

    // Check if we only have the initial "D:"
    if (len < 3)
    {
        // First argument is "program name", search for the last '.' after the
        // last '/', to get the part between.
        int p0 = 0, p1 = 0, i;
        for (i = 0;; i++)
        {
            if (cmd[i] == '/' || cmd[i] == '\\')
                p0 = i + 1;
            else if (cmd[i] == '.')
                p1 = i;
            else if (!cmd[i])
                break;
        }
        if (p1 <= p0)
            p1 = i;
        // Ok, copy chars
        len = 0;
        poke(s, DOS_LBUF + len++, 'D');
        poke(s, DOS_LBUF + len++, ':');
        while (len < 63 && p0 != p1)
        {
            char c = cmd[p0++];
            if (c >= 'a' && c <= 'z')
                poke(s, DOS_LBUF + len++, c - 'a' + 'A');
            else if ((c >= 'A' && c <= 'Z') || c == '_')
                poke(s, DOS_LBUF + len++, c);
        }
        poke(s, DOS_LBUF + len, 0x9B);
        poke(s, DOS_BUFOFF, len);
    }
    else if (len < 63)
    {
        // Normal argument, store as is
        poke(s, DOS_LBUF + len, ' ');
        while (++len < 63 && *cmd)
            poke(s, DOS_LBUF + len, *cmd++);
        poke(s, DOS_LBUF + len, 0x9B);
    }
}

void atari_dos_set_root(sim65 s, const char *path)
{
    if (path)
        root_path = path;
    else
        root_path = ".";
}

void atari_dos_init(sim65 s)
{
    sim65_add_data_rom(s, DISKDV, devhand_emudos, sizeof(devhand_emudos));
    // Store DOS COMTAB
    dpoke(s, 0x0A, COMTAB_BASE);
    dpoke(s, COMTAB_BASE - 10, DOS_LSIO);
    dpoke(s, COMTAB_BASE - 6, DOS_CONVDC);
    poke(s, COMTAB_BASE, 0x4C);
    dpoke(s, COMTAB_BASE + 1, DOS_CP);
    poke(s, COMTAB_BASE + 3, 0x4C);
    dpoke(s, COMTAB_BASE + 4, DOS_CRNAME);
    dpoke(s, COMTAB_BASE + 6, DOS_DIVIO);
    dpoke(s, COMTAB_BASE + 8, DOS_XDIVIO);
    sim65_add_zeroed_ram(s, COMTAB_BASE + 32, 94);
    poke(s, COMTAB_BASE + 10, 2);
    poke(s, COMTAB_BASE + 63, 'D');
    poke(s, COMTAB_BASE + 64, ':');
    poke(s, COMTAB_BASE + 65, 0x9B);
    // Adds callback for the COMTAB handlers
    add_rts_callback(s, DOSCP_BASE, 8, sim_DOS_COMTAB);
    // And for the device handlers
    add_rts_callback(s, DISKD_BASE, 8, sim_DISKD);
    // Add our device to the handler table
    atari_cio_add_hatab(s, 'D', DISKDV);
    // Store our signature to MEMLO, and increase by 16 bytes:
    poke(s, 0x700, 'S');
    dpoke(s, 0x2e7, 0x710);
}
