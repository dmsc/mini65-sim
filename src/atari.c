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
#include "mathpack.h"
#include "hw.h"
#include "sim65.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

// Utility functions
static int cb_error(sim65 s, unsigned addr)
{
    sim65_eprintf(s, "invalid access to cb address $%04x", addr);
    return 0;
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

static unsigned peek(sim65 s, unsigned addr)
{
    return sim65_get_byte(s, addr);
}

static unsigned dpeek(sim65 s, unsigned addr)
{
    return sim65_get_byte(s, addr) + (sim65_get_byte(s, addr + 1) << 8);
}

static void add_rts_callback(sim65 s, unsigned addr, unsigned len, sim65_callback cb)
{
    unsigned char rts = 0x60;
    sim65_add_callback_range(s, addr, len, cb, sim65_cb_exec);
    for (; len > 0; addr++, len--)
        sim65_add_data_rom(s, addr, &rts, 1);
}

static void pchar(unsigned c)
{
    if (c == 0x9b)
        putchar('\n');
    else if (c == 0x12)
        putchar('-');
    else
        putchar(c);
    fflush(stdout);
}

// EDITOR defs
#define LMARGN (0x52) // left margin
#define RMARGN (0x53) // right margin
#define ROWCRS (0x54) // cursor row
#define COLCRS (0x55) // cursor column (2 byte)

#define LO(a) ((a)&0xFF)
#define HI(a) ((a) >> 8)

// HATAB defs
#define HATABS (0x031A) // Device handlers table
#define EDITRV (0xE400)
#define SCRENV (0xE410)
#define KEYBDV (0xE420)
#define PRINTV (0xE430)
#define CASETV (0xE440)
#define DISKDV (0xE3F0) // Emulated DOS, does not exists in real hardware!
#define EDITOR_OFFSET (6) // NOTE: Editor must be entry at offset "6"
#define HATABS_ENTRY(a, b) (a), LO(b), HI(b)
static const unsigned char hatab_default[] = {
    HATABS_ENTRY('P', PRINTV),
    HATABS_ENTRY('C', CASETV),
    HATABS_ENTRY('E', EDITRV), // NOTE: Editor must be entry at offset "6"
    HATABS_ENTRY('S', SCRENV),
    HATABS_ENTRY('K', KEYBDV),
    HATABS_ENTRY('D', DISKDV),
    HATABS_ENTRY(0, 0),
    HATABS_ENTRY(0, 0),
    HATABS_ENTRY(0, 0),
    HATABS_ENTRY(0, 0),
    HATABS_ENTRY(0, 0)
};

// Fake routines addresses:
// Bases
#define EDITR_BASE 0xE500
#define SCREN_BASE 0xE508
#define KEYBD_BASE 0xE510
#define PRINT_BASE 0xE518
#define CASET_BASE 0xE520
#define DISKD_BASE 0xE528
// Offsets
#define DEVR_OPEN (0)
#define DEVR_CLOSE (1)
#define DEVR_GET (2)
#define DEVR_PUT (3)
#define DEVR_STATUS (4)
#define DEVR_SPECIAL (5)
#define DEVR_INIT (6)

#define DEVH_E(a) LO(a - 1), HI(a - 1)
#define DEVH_TAB(a)                      \
    DEVH_E(a##_BASE + DEVR_OPEN),        \
        DEVH_E(a##_BASE + DEVR_CLOSE),   \
        DEVH_E(a##_BASE + DEVR_GET),     \
        DEVH_E(a##_BASE + DEVR_PUT),     \
        DEVH_E(a##_BASE + DEVR_STATUS),  \
        DEVH_E(a##_BASE + DEVR_SPECIAL), \
        0x20, DEVH_E(a##_BASE + DEVR_INIT + 1), 0x00

// Standard Handlers
static const unsigned char devhand_tables[] = {
    DEVH_TAB(EDITR),
    DEVH_TAB(SCREN),
    DEVH_TAB(KEYBD),
    DEVH_TAB(PRINT),
    DEVH_TAB(CASET)
};

// Emulated DOS handler
static const unsigned char devhand_emudos[] = {
    DEVH_TAB(DISKD)
};

// IOCB defs
#define CIOV (0xE456)
#define CIOERR (0xE530) // FIXME: CIO error return routine
#define IC(a) (regs->x + IC##a) // IOCB address
#define GET_IC(a) sim65_get_byte(s, IC(a))
#define CIOCHR (0x2F)  //         ;CHARACTER BYTE FOR CURRENT OPERATION
#define ICHID (0x0340) //         ;HANDLER INDEX NUMBER (FF=IOCB FREE)
#define ICDNO (0x0341) //         ;DEVICE NUMBER (DRIVE NUMBER)
#define ICCOM (0x0342) //         ;COMMAND CODE
#define ICSTA (0x0343) //         ;STATUS OF LAST IOCB ACTION
#define ICBAL (0x0344) //         ;1-byte low buffer address
#define ICBAH (0x0345) //         ;1-byte high buffer address
#define ICPTL (0x0346) //         ;1-byte low PUT-BYTE routine address - 1
#define ICPTH (0x0347) //         ;1-byte high PUT-BYTE routine address - 1
#define ICBLL (0x0348) //         ;1-byte low buffer length
#define ICBLH (0x0349) //         ;1-byte high buffer length
#define ICAX1 (0x034A) //         ;1-byte first auxiliary information
#define ICAX2 (0x034B) //         ;1-byte second auxiliary information
#define ICAX3 (0x034C) //         ;1-byte third auxiliary information
#define ICAX4 (0x034D) //         ;1-byte fourth auxiliary information
#define ICAX5 (0x034E) //         ;1-byte fifth auxiliary information
#define ICSPR (0x034F) //         ;SPARE BYTE
static const unsigned char iocv_empty[16] = {
    0xFF, 0, 0, 0, // HID, DNO, COM, STA
    0, 0, LO(CIOERR - 1), HI(CIOERR - 1), // BAL, BAH, PTL, PTH
    0, 0, 0, 0, // BLL, BLH, AX1, AX2
    0, 0, 0, 0 // AX3, AX4, AX5, SPR
};

static int cio_exit(sim65 s, struct sim65_reg *regs)
{
    poke(s, IC(STA), regs->y);
    if (regs->y & 0x80)
        sim65_set_flags(s, SIM65_FLAG_N, SIM65_FLAG_N);
    else
        sim65_set_flags(s, SIM65_FLAG_N, 0);
    return 0;
}

static int cio_error(sim65 s, struct sim65_reg *regs, const char *err, unsigned value)
{
    regs->y = value & 0xFF;
    sim65_dprintf(s, "CIO: %s", err);
    return cio_exit(s, regs);
}

static int cio_ok(sim65 s, struct sim65_reg *r, unsigned acc)
{
    r->a = acc & 0xFF;
    r->y = 1;
    return cio_exit(s, r);
}

// Calls through DEVTAB offset
static void call_devtab(sim65 s, struct sim65_reg *regs, uint16_t devtab, int fn)
{
    if (fn == DEVR_PUT)
        poke(s, CIOCHR, regs->a);
    sim65_call(s, regs, 1 + dpeek(s, devtab + 2 * fn));
    if (fn == DEVR_GET)
        poke(s, CIOCHR, regs->a);
}

static int sim_CIOV(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    if (regs->x & 0x0F || regs->x > 0x80)
        return cio_error(s, regs, "invalid value of X register", 134);

    unsigned hid  = GET_IC(HID);
    unsigned badr = GET_IC(BAL) | (GET_IC(BAH) << 8);
    unsigned blen = GET_IC(BLL) | (GET_IC(BLH) << 8);
    unsigned com  = GET_IC(COM);
    unsigned ax1  = GET_IC(AX1);

    unsigned devtab = dpeek(s, 1 + hid + HATABS);

    if (hid == 0xFF && com != 3 && com < 12)
        return cio_error(s, regs, "channel not open", 133);

    if (com < 3 || com > 14)
    {
        sim65_dprintf(s, "CIO CMD = %d", com);
        if (com == 37)
        {
            unsigned ax3 = GET_IC(AX3);
            unsigned ax4 = GET_IC(AX4);
            unsigned ax5 = GET_IC(AX5);
            sim65_dprintf(s, "POINT %d/%d/%d", ax3, ax4, ax5);
            return cio_ok(s, regs, 0);
        }
        return cio_error(s, regs, "invalid command", 132);
    }

    if ((com >= 4) && (com < 8) && !(ax1 & 0x4))
        return cio_error(s, regs, "write only", 131);

    if ((com >= 8) && (com < 12) && !(ax1 & 0x8))
        return cio_error(s, regs, "read only", 135);

    // Commands
    if (com == 3)
    {
        sim65_dprintf(s, "CIO open %c%c", peek(s, badr), peek(s, badr + 1));
        // OPEN (command 0)
        if (GET_IC(HID) != 0xFF)
            return cio_error(s, regs, "channel already opened", 129);
        // Search handle and call open routine.
        unsigned dev = peek(s, badr);
        unsigned num = peek(s, badr + 1) - '0';
        if (num > 9)
            num = 0;

        // Store DeviceNumber from filename
        poke(s, IC(DNO), num);

        // Search HATAB
        int i;
        for (i = 0; i < 32; i += 3)
        {
            if (peek(s, HATABS + i) == dev)
            {
                // Copy data
                unsigned devtab = dpeek(s, 1 + i + HATABS);
                poke(s, IC(HID), i);
                dpoke(s, IC(PTL), dpeek(s, devtab + 6));
                // Found, call open
                call_devtab(s, regs, devtab, DEVR_OPEN);
                return cio_exit(s, regs);
            }
        }
        // Return error
        regs->y = 0x82;
        return cio_exit(s, regs);
    }
    else if (com == 4 || com == 5)
    {
        // GET RECORD
        for (;;)
        {
            // Get single
            call_devtab(s, regs, devtab, DEVR_GET);
            if (regs->y & 0x80)
                break;
            if (blen)
            {
                poke(s, badr, regs->a);
                badr++;
                blen--;
            }
            if (regs->a == 0x9B)
                break;
        }
        dpoke(s, IC(BLL), dpeek(s, IC(BLL)) - blen);
        return cio_exit(s, regs);
    }
    else if (com == 6 || com == 7)
    {
        // GET CHARS
        if (!blen)
        {
            // Get single
            call_devtab(s, regs, devtab, DEVR_GET);
            return cio_exit(s, regs);
        }
        else
        {
            while (blen)
            {
                // get
                call_devtab(s, regs, devtab, DEVR_GET);
                if (regs->y & 0x80)
                    break;
                poke(s, badr, regs->a);
                badr++;
                blen--;
            }
            // Must return number of bytes transfered
            sim65_dprintf(s, "ICBL ends at %04x, transfered %d bytes",
                          blen, dpeek(s, IC(BLL)) - blen);
            dpoke(s, IC(BLL), dpeek(s, IC(BLL)) - blen);
            return cio_exit(s, regs);
        }
    }
    else if (com == 8 || com == 9)
    {
        // PUT RECORD
        if (!blen)
        {
            // Put single
            regs->a = 0x9B;
            call_devtab(s, regs, devtab, DEVR_PUT);
            return cio_exit(s, regs);
        }
        else
        {
            while (blen)
            {
                regs->a = peek(s, badr);
                call_devtab(s, regs, devtab, DEVR_PUT);
                if (regs->y & 0x80)
                    break;
                badr++;
                blen--;
                if (peek(s, CIOCHR) == 0x9B)
                    break;
            }
            // Must return number of bytes transfered
            dpoke(s, IC(BLL), dpeek(s, IC(BLL)) - blen);
            return cio_exit(s, regs);
        }
    }
    else if (com == 10 || com == 11)
    {
        // PUT CHARS
        if (!blen)
        {
            // Put single
            call_devtab(s, regs, devtab, DEVR_PUT);
            return cio_exit(s, regs);
        }
        else
        {
            while (blen)
            {
                regs->a = peek(s, badr);
                call_devtab(s, regs, devtab, DEVR_PUT);
                if (regs->y & 0x80)
                    break;
                badr++;
                blen--;
            }
            // Must return number of bytes transfered
            dpoke(s, IC(BLL), dpeek(s, IC(BLL)) - blen);
            return cio_exit(s, regs);
        }
    }
    else if (com == 12)
    {
        // CLOSE
        if (GET_IC(HID) != 0xFF)
        {
            // Call close handler
            call_devtab(s, regs, devtab, DEVR_CLOSE);
        }
        sim65_add_data_ram(s, IC(HID), iocv_empty, 16);
        return cio_ok(s, regs, 0);
    }
    else if (com == 13)
    {
        // GET STATUS
    }
    else if (com == 14)
    {
        // SPECIAL
    }

    return 0;
}

static int sim_CIOERR(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    sim65_dprintf(s, "CIO error, IOCB not open");
    regs->y = 0x83;
    sim65_set_flags(s, SIM65_FLAG_N, SIM65_FLAG_N);
    return 0;
}

static int sim_EDITR(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    static uint8_t last_row = 0;
    switch (addr & 7)
    {
        case DEVR_OPEN:
            sim65_dprintf(s, "EDITR cmd OPEN");
            return 0;
        case DEVR_CLOSE:
            return 0;
        case DEVR_GET:
        {
            int c   = getchar();
            regs->y = 1;
            if (c == EOF)
                regs->y = 136;
            else if (c == '\n')
                regs->a = 0x9b;
            else
                regs->a = c;
            return 0;
        }
        case DEVR_PUT:
            // Keeps column number updated
            dpoke(s, COLCRS, dpeek(s, COLCRS) + 1);
            if (regs->a == 0x9B)
            {
                dpoke(s, COLCRS, peek(s, LMARGN));
                if (peek(s, ROWCRS) < 24)
                    poke(s, ROWCRS, peek(s, ROWCRS) + 1);
            }
            else if (peek(s, ROWCRS) != last_row)
                pchar(0x9B);
            last_row = peek(s, ROWCRS);
            pchar(regs->a);
            regs->y = 1;
            return 0;
        case DEVR_STATUS:
            sim65_dprintf(s, "EDITR cmd STATUS");
            return 0;
        case DEVR_SPECIAL:
            sim65_dprintf(s, "EDITR cmd SPECIAL");
            return 0; // Nothing
        case DEVR_INIT:
            sim65_dprintf(s, "EDITR cmd INIT");
            return 0;
        default:
            return cb_error(s, addr);
    }
}

static int sim_SCREN(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    switch (addr & 7)
    {
        case DEVR_OPEN:
            sim65_dprintf(s, "SCREN cmd OPEN #%d, %d, %d",
                         (regs->x >> 4), GET_IC(AX1), GET_IC(AX2));
            return 0;
        case DEVR_CLOSE:
            return 0;
        case DEVR_GET:
            sim65_dprintf(s, "SCREN cmd GET");
            return 0;
        case DEVR_PUT:
            sim65_dprintf(s, "SCREN cmd PUT %d", regs->a);
            regs->y = 1;
            return 0;
        case DEVR_STATUS:
            sim65_dprintf(s, "SCREN cmd STATUS");
            return 0;
        case DEVR_SPECIAL:
            sim65_dprintf(s, "SCREN cmd SPECIAL");
            return 0;
        case DEVR_INIT:
            return 0;
        default:
            return cb_error(s, addr);
    }
}

static int sim_KEYBD(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    switch (addr & 7)
    {
        case DEVR_OPEN:
            return 0;
        case DEVR_CLOSE:
            return 0;
        case DEVR_GET:
        {
            int c   = getchar();
            regs->y = 1;
            if (c == EOF)
                regs->y = 136;
            else if (c == '\n')
                regs->a = 0x9b;
            else
                regs->a = c;
            return 0;
        }
        case DEVR_PUT:
            regs->y = 135;
            return 0; // Nothing
        case DEVR_STATUS:
            return 0;
        case DEVR_SPECIAL:
            return 0; // Nothing
        case DEVR_INIT:
            return 0;
        default:
            return cb_error(s, addr);
    }
}

static int sim_PRINT(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    switch (addr & 7)
    {
        case DEVR_OPEN:
            return 0;
        case DEVR_CLOSE:
            return 0;
        case DEVR_GET:
            return 0; // Nothing
        case DEVR_PUT:
            return 0;
        case DEVR_STATUS:
            return 0;
        case DEVR_SPECIAL:
            return 0; // Nothing
        case DEVR_INIT:
            return 0;
        default:
            return cb_error(s, addr);
    }
}

static int sim_CASET(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    switch (addr & 7)
    {
        case DEVR_OPEN:
            return 0;
        case DEVR_CLOSE:
            return 0;
        case DEVR_GET:
            return 0;
        case DEVR_PUT:
            return 0;
        case DEVR_STATUS:
            return 0;
        case DEVR_SPECIAL:
            return 0;
        case DEVR_INIT:
            return 0;
        default:
            return cb_error(s, addr);
    }
}

static int sim_DISKD(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    // Store one file handle for each CIO channel
    static FILE *fhand[16];

    // We need IOCB data
    unsigned chn  = (regs->x >> 4);
    unsigned badr = GET_IC(BAL) | (GET_IC(BAH) << 8);
    unsigned dno  = GET_IC(DNO);
    unsigned ax1  = GET_IC(AX1);
    unsigned ax2  = GET_IC(AX2);

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
            fhand[chn] = fopen(fname, flags);
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
            return 0;
        case DEVR_INIT:
            return 0;
        default:
            return cb_error(s, addr);
    }
}

static int sim_RTCLOK(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    static int startTime;

    // Get current time
    struct timeval tv;
    gettimeofday(&tv, 0);
    int curTime   = (int)(fmod(tv.tv_sec * 60 + tv.tv_usec * 0.00006, 16777216.));
    int atariTime = curTime - startTime;

    if (data == sim65_cb_read)
    {
        if (addr == 0x12)
            return 0xFF & (atariTime >> 16);
        else if (addr == 0x13)
            return 0xFF & (atariTime >> 8);
        else
            return 0xFF & atariTime;
    }
    else
    {
        if (addr == 0x12)
            atariTime = (atariTime & 0x00FFFF) | (data << 16);
        else if (addr == 0x13)
            atariTime = (atariTime & 0xFF00FF) | (data << 8);
        else
            atariTime = (atariTime & 0xFFFF00) | data;
        startTime = curTime - atariTime;
    }
    return 0;
}

// Define our memory map, usable for applications 46.25k.
#define MAX_RAM  (0xD000)       // RAM up to 0xD000 (52k)
#define APP_RAM  (0xC000)       // Usable to applications 0xC000 (48k)
#define LOW_RAM  (0x0700)       // Reserved to the OS up to 0x0700 (1.75k)
#define VID_RAM  (0xC000)       // Video RAM from 0xC000) (4k reserved for video)

static void atari_bios_init(sim65 s)
{
    unsigned i;
    // Adds 32 bytes of zeroed RAM at $80
    sim65_add_zeroed_ram(s, 0x80, 0x20);
    // CIOV
    add_rts_callback(s, CIOV, 1, sim_CIOV);
    // Init empty
    for (i = 0; i < 8; i++)
        sim65_add_data_ram(s, ICHID + i * 16, iocv_empty, 16);
    // Copy HTAB table
    sim65_add_data_rom(s, HATABS, hatab_default, sizeof(hatab_default));
    // Copy device handlers table
    sim65_add_data_rom(s, EDITRV, devhand_tables, sizeof(devhand_tables));
    sim65_add_data_rom(s, DISKDV, devhand_emudos, sizeof(devhand_emudos));
    // Init IOCV 0, editor
    poke(s, ICHID, EDITOR_OFFSET);
    poke(s, ICAX1, 0x0C);
    dpoke(s, ICPTL, dpeek(s, EDITRV + 6));
    // Install device handlers callbacks
    add_rts_callback(s, EDITR_BASE, 8, sim_EDITR);
    add_rts_callback(s, SCREN_BASE, 8, sim_SCREN);
    add_rts_callback(s, KEYBD_BASE, 8, sim_KEYBD);
    add_rts_callback(s, PRINT_BASE, 8, sim_PRINT);
    add_rts_callback(s, CASET_BASE, 8, sim_CASET);
    add_rts_callback(s, DISKD_BASE, 8, sim_DISKD);
    add_rts_callback(s, CIOERR, 1, sim_CIOERR);
    // Math Package
    fp_init(s);
    // Random OS addresses
    sim65_add_callback_range(s, 0x12, 3, sim_RTCLOK, sim65_cb_read);
    sim65_add_callback_range(s, 0x12, 3, sim_RTCLOK, sim65_cb_write);
    poke(s, 8, 0); // WARM START
    poke(s, 17, 0x80); // BREAK key not pressed
    dpoke(s, 10, 0x0); // DOSVEC, go to DOS vector, use 0 as simulation return
    dpoke(s, 14, 0x800); // APPHI, lowest usable RAM area
    dpoke(s, 0x2e5, APP_RAM); // MEMTOP
    dpoke(s, 0x2e7, LOW_RAM); // MEMLO
    poke(s, 0x2FC, 0xFF); // CH
    poke(s, 0x2F2, 0xFF); // CH1
    poke(s, LMARGN, 2); // LMARGIN
    poke(s, RMARGN, 39); // RMARGIN
    poke(s, ROWCRS, 0); // ROWCRS
    dpoke(s, COLCRS, 2); // COLCRS
    poke(s, 0x57, 0); // DINDEX
    dpoke(s, 0x58, VID_RAM); // Simulated screen pointer
    poke(s, 0x5D, 0); // OLDCH
    dpoke(s, 0x5E, VID_RAM); // Store an invalid value in OLDADR, to catch
                            // programs writing to the screen directly.
    poke(s, 0x6A, APP_RAM/256); // RAMTOP
    poke(s, 0x2be, 64); // SHFLOK
}


static const struct
{
    const char *lbl;
    unsigned addr;
} atari_labels[] = {

    { "WARMST",   0x08 }, // WARM START FLAG
    { "BOOTQ",    0x09 }, // SUCCESSFUL BOOT FLAG
    { "DOSVEC",   0x0A }, // DISK SOFTWARE START VECTOR
    { "DOSINI",   0x0C }, // DISK SOFTWARE INIT ADDRESS
    { "APPMHI",   0x0E }, // APPLICATIONS MEMORY HI LIMIT

    { "POKMSK",   0x10 }, // SYSTEM MASK FOR POKEY IRQ ENABLE (shadow of IRQEN)
    { "BRKKEY",   0x11 }, // BREAK KEY FLAG
    { "RTCLOK",   0x12 }, // REAL TIME CLOCK (IN 16 MSEC UNITS>
    { "BUFADR",   0x15 }, // INDIRECT BUFFER ADDRESS REGISTER
    { "ICCOMT",   0x17 }, // COMMAND FOR VECTOR
    { "DSKFMS",   0x18 }, // DISK FILE MANAGER POINTER
    { "DSKUTL",   0x1A }, // DISK UTILITIES POINTER
    { "ABUFPT",   0x1C }, // ##1200xl## 4-byte ACMI buffer pointer area

    { "IOCBAS",   0x20 }, // 16-byte page zero IOCB
    { "ICHIDZ",   0x20 }, // HANDLER INDEX NUMBER (FF = IOCB FREE)
    { "ICDNOZ",   0x21 }, // DEVICE NUMBER (DRIVE NUMBER)
    { "ICCOMZ",   0x22 }, // COMMAND CODE
    { "ICSTAZ",   0x23 }, // STATUS OF LAST IOCB ACTION
    { "ICBALZ",   0x24 }, // BUFFER ADDRESS LOW BYTE
    { "ICBAHZ",   0x25 }, // 1-byte high buffer address
    { "ICPTLZ",   0x26 }, // PUT BYTE ROUTINE ADDRESS -1
    { "ICPTHZ",   0x27 }, // 1-byte high PUT-BYTE routine address
    { "ICBLLZ",   0x28 }, // BUFFER LENGTH LOW BYTE
    { "ICBLHZ",   0x29 }, // 1-byte high buffer length
    { "ICAX1Z",   0x2A }, // AUXILIARY INFORMATION FIRST BYTE
    { "ICAX2Z",   0x2B }, // 1-byte second auxiliary information
    { "ICSPRZ",   0x2C }, // 4-byte spares
    { "ICIDNO",   0x2E }, // IOCB NUMBER X 16
    { "CIOCHR",   0x2F }, // CHARACTER BYTE FOR CURRENT OPERATION

    { "CRITIC",   0x42 }, // DEFINES CRITICAL SECTION (CRITICAL IF NON-Z)

    { "ATRACT",   0x4D }, // ATRACT FLAG
    { "DRKMSK",   0x4E }, // DARK ATRACT MASK
    { "COLRSH",   0x4F }, // ATRACT COLOR SHIFTER (EOR'ED WITH PLAYFIELD

    { "LMARGN",   0x52 }, // left margin (normally 2, cc65 C startup code sets it to 0)
    { "RMARGN",   0x53 }, // right margin (normally 39 if no XEP80 is used)
    { "ROWCRS",   0x54 }, // 1-byte cursor row
    { "COLCRS",   0x55 }, // 2-byte cursor column
    { "DINDEX",   0x57 }, // 1-byte display mode
    { "SAVMSC",   0x58 }, // 2-byte saved memory scan counter
    { "OLDROW",   0x5A }, // 1-byte prior row
    { "OLDCOL",   0x5B }, // 2-byte prior column
    { "OLDCHR",   0x5D }, // DATA UNDER CURSOR
    { "OLDADR",   0x5E }, // 2-byte saved cursor memory address

    { "RAMTOP",   0x6A }, // RAM SIZE DEFINED BY POWER ON LOGIC

    { "FR0",      0xD4 }, // 6-byte register 0
    { "FR0+1",    0xD5 }, // 6-byte register 0
    { "FR0+2",    0xD6 }, // 6-byte register 0
    { "FR0+3",    0xD7 }, // 6-byte register 0
    { "FR0+4",    0xD8 }, // 6-byte register 0
    { "FR0+5",    0xD9 }, // 6-byte register 0
    { "FRE",      0xDA }, // 6-byte (internal) register E

    { "FR1",      0xE0 }, // FP REG1
    { "FR1+1",    0xE1 }, // FP REG1
    { "FR1+2",    0xE2 }, // FP REG1
    { "FR1+3",    0xE3 }, // FP REG1
    { "FR1+4",    0xE4 }, // FP REG1
    { "FR1+5",    0xE5 }, // FP REG1

    { "FR2",      0xE6 }, // 6-byte (internal) register 2

    { "CIX",      0xF2 }, // CURRENT INPUT INDEX
    { "INBUFF",   0xF3 }, // POINTS TO USER'S LINE INPUT BUFFER
    { "INBUFF+1", 0xF4 }, // POINTS TO USER'S LINE INPUT BUFFER

    // Most of the following are not used in the simulator,
    // but included anyway for debugging.

    { "VDSLST", 0x0200 }, // DISPLAY LIST NMI VECTOR
    { "VPRCED", 0x0202 }, // PROCEED LINE IRQ VECTOR
    { "VINTER", 0x0204 }, // INTERRUPT LINE IRQ VECTOR
    { "VBREAK", 0x0206 }, // SOFTWARE BREAK (00) INSTRUCTION IRQ VECTOR
    { "VKEYBD", 0x0208 }, // POKEY KEYBOARD IRQ VECTOR
    { "VSERIN", 0x020A }, // POKEY SERIAL INPUT READY IRQ
    { "VSEROR", 0x020C }, // POKEY SERIAL OUTPUT READY IRQ
    { "VSEROC", 0x020E }, // POKEY SERIAL OUTPUT COMPLETE IRQ
    { "VTIMR1", 0x0210 }, // POKEY TIMER 1 IRQ
    { "VTIMR2", 0x0212 }, // POKEY TIMER 2 IRQ
    { "VTIMR4", 0x0214 }, // POKEY TIMER 4 IRQ
    { "VIMIRQ", 0x0216 }, // IMMEDIATE IRQ VECTOR
    { "CDTMV1", 0x0218 }, // COUNT DOWN TIMER 1
    { "CDTMV2", 0x021A }, // COUNT DOWN TIMER 2
    { "CDTMV3", 0x021C }, // COUNT DOWN TIMER 3
    { "CDTMV4", 0x021E }, // COUNT DOWN TIMER 4
    { "CDTMV5", 0x0220 }, // COUNT DOWN TIMER 5
    { "VVBLKI", 0x0222 }, // IMMEDIATE VERTICAL BLANK NMI VECTOR
    { "VVBLKD", 0x0224 }, // DEFERRED VERTICAL BLANK NMI VECTOR
    { "CDTMA1", 0x0226 }, // COUNT DOWN TIMER 1 JSR ADDRESS
    { "CDTMA2", 0x0228 }, // COUNT DOWN TIMER 2 JSR ADDRESS
    { "CDTMF3", 0x022A }, // COUNT DOWN TIMER 3 FLAG
    { "SRTIMR", 0x022B }, // SOFTWARE REPEAT TIMER
    { "CDTMF4", 0x022C }, // COUNT DOWN TIMER 4 FLAG

    { "SDMCTL", 0x022F }, // SAVE DMACTL REGISTER
    { "SDLSTL", 0x0230 }, // SAVE DISPLAY LIST LOW BYTE
    { "SDLSTH", 0x0231 }, // SAVE DISPLAY LIST HI BYTE
    { "SSKCTL", 0x0232 }, // SKCTL REGISTER RAM
    { "LPENH",  0x0234 }, // LIGHT PEN HORIZONTAL VALUE
    { "LPENV",  0x0235 }, // LIGHT PEN VERTICAL VALUE
    { "BRKKY",  0x0236 }, // BREAK KEY VECTOR

    { "PADDL0", 0x0270 }, // 1-byte potentiometer 0
    { "PADDL1", 0x0271 }, // 1-byte potentiometer 1
    { "PADDL2", 0x0272 }, // 1-byte potentiometer 2
    { "PADDL3", 0x0273 }, // 1-byte potentiometer 3
    { "PADDL4", 0x0274 }, // 1-byte potentiometer 4
    { "PADDL5", 0x0275 }, // 1-byte potentiometer 5
    { "PADDL6", 0x0276 }, // 1-byte potentiometer 6
    { "PADDL7", 0x0277 }, // 1-byte potentiometer 7

    { "STICK0", 0x0278 }, // 1-byte joystick 0
    { "STICK1", 0x0279 }, // 1-byte joystick 1
    { "STICK2", 0x027A }, // 1-byte joystick 2
    { "STICK3", 0x027B }, // 1-byte joystick 3

    { "PTRIG0", 0x027C }, // 1-byte paddle trigger 0
    { "PTRIG1", 0x027D }, // 1-byte paddle trigger 1
    { "PTRIG2", 0x027E }, // 1-byte paddle trigger 2
    { "PTRIG3", 0x027F }, // 1-byte paddle trigger 3
    { "PTRIG4", 0x0280 }, // 1-byte paddle trigger 4
    { "PTRIG5", 0x0281 }, // 1-byte paddle trigger 5
    { "PTRIG6", 0x0281 }, // 1-byte paddle trigger 6
    { "PTRIG7", 0x0283 }, // 1-byte paddle trigger 7

    { "STRIG0", 0x0284 }, // 1-byte joystick trigger 0
    { "STRIG1", 0x0285 }, // 1-byte joystick trigger 1
    { "STRIG2", 0x0286 }, // 1-byte joystick trigger 2
    { "STRIG3", 0x0287 }, // 1-byte joystick trigger 3

    // Text window
    { "TXTROW", 0x0290 }, // TEXT ROWCRS
    { "TXTCOL", 0x0291 }, // TEXT COLCRS
    { "TINDEX", 0x0293 }, // TEXT INDEX
    { "TXTMSC", 0x0294 }, // FOOLS CONVRT INTO NEW MSC
    { "TXTOLD", 0x0296 }, // OLDROW & OLDCOL FOR TEXT (AND THEN SOME)

    // Color registers
    { "PCOLR0", 0x02C0 }, // 1-byte player-missile 0 color/luminance
    { "PCOLR1", 0x02C1 }, // 1-byte player-missile 1 color/luminance
    { "PCOLR2", 0x02C2 }, // 1-byte player-missile 2 color/luminance
    { "PCOLR3", 0x02C3 }, // 1-byte player-missile 3 color/luminance
    { "COLOR0", 0x02C4 }, // 1-byte playfield 0 color/luminance
    { "COLOR1", 0x02C5 }, // 1-byte playfield 1 color/luminance
    { "COLOR2", 0x02C6 }, // 1-byte playfield 2 color/luminance
    { "COLOR3", 0x02C7 }, // 1-byte playfield 3 color/luminance
    { "COLOR4", 0x02C8 }, // 1-byte background color/luminance

    { "RUNAD",    0x02E0 }, // Binary load RUN ADDRESS
    { "INITAD",   0x02E2 }, // Binary load INIT ADDRESS
    { "RAMSIZ",   0x02E4 }, // RAM SIZE (HI BYTE ONLY)
    { "MEMTOP",   0x02E5 }, // TOP OF AVAILABLE USER MEMORY
    { "MEMTOP+1", 0x02E6 },
    { "MEMLO",    0x02E7 }, // BOTTOM OF AVAILABLE USER MEMORY
    { "MEMLO+1",  0x02E8 },

    { "CHAR",     0x02FA }, // 1-byte internal character
    { "ATACHR",   0x02FB }, // ATASCII CHARACTER FOR DRAW/FILL BORDER
    { "CH",       0x02FC }, // GLOBAL VARIABLE FOR KEYBOARD
    { "FILDAT",   0x02FD }, // RIGHT FILL COLOR
    { "DSPFLG",   0x02FE }, // DISPLAY FLAG   DISPLAY CNTLS IF NON-ZERO
    { "SSFLAG",   0x02FF }, // START/STOP FLAG FOR PAGING (CNTL 1). CLEARE

    { "HATABS", 0x031A }, // 35-byte handler address table (was 38 bytes)


    { "IOCB",  0x0340 }, // I/O CONTROL BLOCKS
    { "ICHID", 0x0340 }, // HANDLER INDEX NUMBER (FF=IOCB FREE)
    { "ICDNO", 0x0341 }, // DEVICE NUMBER (DRIVE NUMBER)
    { "ICCOM", 0x0342 }, // COMMAND CODE
    { "ICSTA", 0x0343 }, // STATUS OF LAST IOCB ACTION
    { "ICBAL", 0x0344 }, // 1-byte low buffer address
    { "ICBAH", 0x0345 }, // 1-byte high buffer address
    { "ICPTL", 0x0346 }, // 1-byte low PUT-BYTE routine address - 1
    { "ICPTH", 0x0347 }, // 1-byte high PUT-BYTE routine address - 1
    { "ICBLL", 0x0348 }, // 1-byte low buffer length
    { "ICBLH", 0x0349 }, // 1-byte high buffer length
    { "ICAX1", 0x034A }, // 1-byte first auxiliary information
    { "ICAX2", 0x034B }, // 1-byte second auxiliary information
    { "ICAX3", 0x034C }, // 1-byte third auxiliary information
    { "ICAX4", 0x034D }, // 1-byte fourth auxiliary information
    { "ICAX5", 0x034E }, // 1-byte fifth auxiliary information
    { "ICSPR", 0x034F }, // SPARE BYTE

    { "LBPR1", 0x057E }, // LBUFF PREFIX 1
    { "LBPR2", 0x057F }, // LBUFF PREFIX 2
    { "LBUFF", 0x0580 }, // 128-byte line buffer

    // Floating Point package
    { "AFP",    0xD800 }, // convert ASCII to floating point
    { "FASC",   0xD8E6 }, // convert floating point to ASCII
    { "IFP",    0xD9AA }, // convert integer to floating point
    { "FPI",    0xD9D2 }, // convert floating point to integer
    { "ZFR0",   0xDA44 }, // zero FR0
    { "ZF1",    0xDA46 }, // zero floating point number
    { "FSUB",   0xDA60 }, // subtract floating point numbers
    { "FADD",   0xDA66 }, // add floating point numbers
    { "FMUL",   0xDADB }, // multiply floating point numbers
    { "FDIV",   0xDB28 }, // divide floating point numbers
    { "PLYEVL", 0xDD40 }, // evaluate floating point polynomial
    { "FLD0R",  0xDD89 }, // load floating point number
    { "FLD0P",  0xDD8D }, // load floating point number
    { "FLD1R",  0xDD98 }, // load floating point number
    { "PLD1P",  0xDD9C }, // load floating point number
    { "FST0R",  0xDDA7 }, // store floating point number
    { "FST0P",  0xDDAB }, // store floating point number
    { "FMOVE",  0xDDB6 }, // move floating point number
    { "LOG",    0xDECD }, // calculate floating point logarithm
    { "LOG10",  0xDED1 }, // calculate floating point base 10 logarithm
    { "EXP",    0xDDC0 }, // calculate floating point exponential
    { "EXP10",  0xDDCC }, // calculate floating point base 10 exponential

    { "EDITRV", 0xE400 }, // editor handler vector table
    { "SCRENV", 0xE410 }, // screen handler vector table
    { "KEYBDV", 0xE420 }, // keyboard handler vector table
    { "PRINTV", 0xE430 }, // printer handler vector table
    { "CASETV", 0xE440 }, // cassette handler vector table

    { "DISKIV", 0xE450 }, // vector to initialize DIO
    { "DSKINV", 0xE453 }, // vector to DIO
    { "CIOV",   0xE456 }, // vector to CIO
    { "SIOV",   0xE459 }, // vector to SIO
    { "SETVBV", 0xE45C }, // vector to set VBLANK parameters
    { "SYSVBV", 0xE45F }, // vector to process immediate VBLANK
    { "XITVBV", 0xE462 }, // vector to process deferred VBLANK
    { "SIOINV", 0xE465 }, // vector to initialize SIO
    { "SENDEV", 0xE468 }, // vector to enable SEND
    { "INTINV", 0xE46B }, // vector to initialize interrupt handler
    { "CIOINV", 0xE46E }, // vector to initialize CIO
    { "BLKBDV", 0xE471 }, // vector to power-up display
    { "WARMSV", 0xE474 }, // vector to warmstart
    { "COLDSV", 0xE477 }, // vector to coldstart
    { "RBLOKV", 0xE47A }, // vector to read cassette block
    { "CSOPIV", 0xE47D }, // vector to open cassette for input

    { 0, 0 }
};

void atari_init(sim65 s, int load_labels)
{
    // Add 52k of uninitialized ram, maximum possible for the Atari architecture.
    sim65_add_ram(s, 0, MAX_RAM);
    // Add hardware handlers
    atari_hardware_init(s);
    // Add ROM handlers
    atari_bios_init(s);
    // Load labels
    if (load_labels)
    {
        for (int i = 0; 0 != atari_labels[i].lbl; i++)
            sim65_lbl_add(s, atari_labels[i].addr, atari_labels[i].lbl);
    }
}

enum sim65_error atari_xex_load(sim65 s, const char *name)
{
    const uint16_t RUNAD = 0x2E0;
    const uint16_t INITAD = 0x2E2;
    int state = 0, saddr = 0, eaddr = 0, start = 0, vec;
    FILE *f = fopen(name, "rb");
    if (!f)
        return sim65_err_user;
    // Error return
    enum sim65_error e = sim65_err_none;
    // Store 0 into RUNAD and INITAD
    dpoke(s, RUNAD, 0);
    dpoke(s, INITAD, 0);
    while (e == sim65_err_none)
    {
        unsigned char data;
        int c = getc(f);
        if (c == EOF)
        {
            vec = dpeek(s, RUNAD);
            if (vec != 0)
            {
                sim65_dprintf(s, "call RUN vector at $%04X", vec);
                start = vec;
            }
            else
                sim65_dprintf(s, "call XEX load at $%04X", start);
            // Run start address and exit
            e = sim65_call(s, 0, start);
            break;
        }
        switch (state)
        {
            case 0:     // 0: Read first $FF
            case 1:     // 1: Read second $FF
                if (c != 0xFF)
                    return -1;
                break;
            case 2:     // 2: Read first byte of load address
                saddr = c;
                break;
            case 3:     // 3: Read second byte of load address
                saddr |= c << 8;
                // Store start address - this is the run address if none is given
                start = saddr;
                break;
            case 4:     // 4: Read first byte of load-end address
                eaddr = c;
                break;
            case 5:     // 5: Read second byte of load-end address
                eaddr |= c << 8;
                break;
            case 6:     // 6: Read data from saddr to eaddr
                data = c;
                sim65_add_data_ram(s, saddr, &data, 1);
                if (saddr != eaddr)
                {
                    saddr++;
                    continue;
                }
                // End of data section, test if we have a new INIT address
                vec = dpeek(s, INITAD);
                if (vec != 0)
                {
                    // Execute!
                    sim65_dprintf(s, "call INIT vector at $%04X", vec);
                    e = sim65_call(s, 0, vec);
                    // Celar INITAD
                    dpoke(s, INITAD, 0);
                }
                break;
            case 7:     // 7: Read first byte of new header
                saddr = c;
                break;
            case 8:     // 8: Read second byte of new header, skip if value is $FFFF
                saddr |= c << 8;
                if (saddr == 0xFFFF)
                    state = 2;
                else
                    state = 4;
                continue;
        }
        state++;
    };
    fclose(f);
    return e;
}

int atari_rom_load(sim65 s, int addr, const char *name)
{
    int c, saddr = addr;
    FILE *f = fopen(name, "rb");
    if (!f)
        return sim65_err_user;

    // Load full ROM
    while (EOF != (c = getc(f)))
    {
        unsigned char data = c;
        sim65_add_data_rom(s, addr++, &data, 1);
    }
    fclose(f);

    // Check if we have a standard Atari ROM
    if (saddr == 0xA000 && addr == 0xC000)
    {
        uint16_t flag = dpeek(s, 0xBFFC);
        uint16_t rvec = dpeek(s, 0xBFFA);
        uint16_t ivec = dpeek(s, 0xBFFE);
        sim65_dprintf(s, "load Atari ROM, RUN=%04X FLAG=%04X INIT=%04X", rvec, flag, ivec);
        if ((flag & 0xFF) == 0 && ivec < 0xC000 && ivec >= 0xA000)
        {
            // Run init vector
            sim65_dprintf(s, "call INIT vector at $%04X", ivec);
            enum sim65_error e = sim65_call(s, 0, ivec);
            if (e)
                return e;
        }
        if (rvec < 0xC000 && rvec >= 0xA000)
        {
            sim65_dprintf(s, "call RUN vector at $%04X", rvec);
            return sim65_call(s, 0, rvec);
        }
    }
    // Else, we run from the ROM start
    sim65_dprintf(s, "call ROM start at $%04X", saddr);
    return sim65_call(s, 0, saddr);
}
