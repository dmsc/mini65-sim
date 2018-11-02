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
#include "mathpack.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

// Utility functions
static int cb_error(unsigned addr)
{
    fprintf(stderr, "Invalid access to cb address $%04x\n", addr);
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
        regs->p |= 0x80;
    else
        regs->p &= 0x7F;
    return 0;
}

static int cio_error(sim65 s, struct sim65_reg *regs, const char *err, unsigned value)
{
    regs->y = value & 0xFF;
    fprintf(stderr, "CIO: %s\n", err);
    return cio_exit(s, regs);
}

static int cio_ok(sim65 s, struct sim65_reg *r, unsigned acc)
{
    r->a = acc & 0xFF;
    r->y = 1;
    return cio_exit(s, r);
}

static int sim_CIOV(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    if (data != SIM65_CB_EXEC)
        return cb_error(addr);

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
        fprintf(stderr, "CMD = %d\n", com);
        if (com == 37)
        {
            unsigned ax3 = GET_IC(AX3);
            unsigned ax4 = GET_IC(AX4);
            unsigned ax5 = GET_IC(AX5);
            fprintf(stderr, "POINT %d/%d/%d\n", ax3, ax4, ax5);
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
        fprintf(stderr, "CIO open %c%c\n", peek(s, badr), peek(s, badr + 1));
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
                sim65_call(s, regs, dpeek(s, devtab));
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
            sim65_call(s, regs, dpeek(s, devtab + 4));
            poke(s, 0x2F, regs->a);
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
            sim65_call(s, regs, dpeek(s, devtab + 4));
            poke(s, 0x2F, regs->a);
            return cio_exit(s, regs);
        }
        else
        {
            while (blen)
            {
                // get
                sim65_call(s, regs, dpeek(s, devtab + 4));
                if (regs->y & 0x80)
                    break;
                poke(s, badr, regs->a);
                badr++;
                blen--;
            }
            // Must return number of bytes transfered
            fprintf(stderr, "blen ends at %d, transfered %d\n", blen, dpeek(s, IC(BLL)) - blen);
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
            poke(s, 0x2F, regs->a);
            sim65_call(s, regs, dpeek(s, devtab + 6));
            return cio_exit(s, regs);
        }
        else
        {
            while (blen)
            {
                regs->a = peek(s, badr);
                poke(s, 0x2F, regs->a);
                sim65_call(s, regs, dpeek(s, devtab + 6));
                if (regs->y & 0x80)
                    break;
                badr++;
                blen--;
                if (peek(s, 0x2F) == 0x9B)
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
            poke(s, 0x2F, regs->a);
            sim65_call(s, regs, dpeek(s, devtab + 6));
            return cio_exit(s, regs);
        }
        else
        {
            while (blen)
            {
                regs->a = peek(s, badr);
                poke(s, 0x2F, regs->a);
                sim65_call(s, regs, dpeek(s, devtab + 6));
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
            sim65_call(s, regs, dpeek(s, devtab + 2));
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
    if (data != SIM65_CB_EXEC)
        return cb_error(addr);
    fprintf(stderr, "IOCB NOT OPEN\n");
    regs->y = 0x83;
    regs->p |= 0x80;
    return 0;
}

static int sim_EDITR(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    static int last_row = 0;
    if (data != SIM65_CB_EXEC)
        return cb_error(addr);
    switch (addr & 7)
    {
        case DEVR_OPEN:
            fprintf(stderr, "EDITR cmd OPEN\n");
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
            dpoke(s, 0x55, dpeek(s, 0x55) + 1);
            if (regs->a == 0x9B)
            {
                dpoke(s, 0x55, peek(s, 0x52));
                if (peek(s, 0x54) < 24)
                    poke(s, 0x54, peek(s, 0x54) + 1);
            }
            else if (peek(s, 0x54) != last_row)
                pchar(0x9B);
            last_row = peek(s, 0x54);
            pchar(regs->a);
            regs->y = 1;
            return 0;
        case DEVR_STATUS:
            fprintf(stderr, "EDITR cmd STATUS\n");
            return 0;
        case DEVR_SPECIAL:
            fprintf(stderr, "EDITR cmd SPECIAL\n");
            return 0; // Nothing
        case DEVR_INIT:
            fprintf(stderr, "EDITR cmd INIT\n");
            return 0;
        default:
            return cb_error(addr);
    }
}

static int sim_SCREN(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    if (data != SIM65_CB_EXEC)
        return cb_error(addr);
    switch (addr & 7)
    {
        case DEVR_OPEN:
            fprintf(stderr, "SCREN cmd OPEN #%d, %d, %d\n", (regs->x >> 4), GET_IC(AX1), GET_IC(AX2));
            return 0;
        case DEVR_CLOSE:
            return 0;
        case DEVR_GET:
            fprintf(stderr, "SCREN cmd GET\n");
            return 0;
        case DEVR_PUT:
            fprintf(stderr, "SCREN cmd PUT %d\n", regs->a);
            regs->y = 1;
            return 0;
        case DEVR_STATUS:
            fprintf(stderr, "SCREN cmd STATUS\n");
            return 0;
        case DEVR_SPECIAL:
            fprintf(stderr, "SCREN cmd SPECIAL\n");
            return 0;
        case DEVR_INIT:
            return 0;
        default:
            return cb_error(addr);
    }
}

static int sim_KEYBD(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    if (data != SIM65_CB_EXEC)
        return cb_error(addr);
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
            return cb_error(addr);
    }
}

static int sim_PRINT(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    if (data != SIM65_CB_EXEC)
        return cb_error(addr);
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
            return cb_error(addr);
    }
}

static int sim_CASET(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    if (data != SIM65_CB_EXEC)
        return cb_error(addr);
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
            return cb_error(addr);
    }
}

static int sim_DISKD(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    // Store one file handle for each CIO channel
    static FILE *fhand[16];

    if (data != SIM65_CB_EXEC)
        return cb_error(addr);

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
            fprintf(stderr, "DISK OPEN #%d, %d, %d, '%s'\n", chn, ax1, ax2, fname);
            // Test if not already open
            if (fhand[chn])
            {
                fprintf(stderr, "disk: Internal error, %d already open.\n", chn);
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
                fprintf(stderr, "disk open: error %s\n", strerror(errno));
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
                fprintf(stderr, "disk get: Internal error, %d closed.\n", chn);
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
                fprintf(stderr, "disk put: Internal error, %d closed.\n", chn);
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
            return cb_error(addr);
    }
}

static int sim_RTCLOK(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    static int startTime;

    if (data == SIM65_CB_EXEC)
        return cb_error(addr);

    // Get current time
    struct timeval tv;
    gettimeofday(&tv, 0);
    int curTime   = (int)(fmod(tv.tv_sec * 60 + tv.tv_usec * 0.00006, 16777216.));
    int atariTime = curTime - startTime;

    if (data == SIM65_CB_READ)
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

static int catch_OLDADR_write(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    if (data == SIM65_CB_EXEC)
        return cb_error(addr);

    if (data == SIM65_CB_READ)
        return 0;

    fprintf(stderr, "screen write $%02X (%d)\n", data, addr - 0xE000);
    return 0;
}

int abios_init(sim65 s)
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
    dpoke(s, 0x2e5, 0xC000); // MEMTOP
    dpoke(s, 0x2e7, 0x700); // MEMLO
    poke(s, 0x2FC, 0xFF); // CH
    poke(s, 0x2F2, 0xFF); // CH1
    poke(s, 0x52, 2); // LMARGIN
    poke(s, 0x53, 39); // RMARGIN
    dpoke(s, 0x58, 0xC000); // Simulated screen pointer
    poke(s, 0x5D, 0); // OLDCH
    dpoke(s, 0x5E, 0xC000); // Store an invalid value in OLDADR, to catch
        // programs writing to the screen directly.
    poke(s, 0x6A, 0xC0); // RAMTOP
    sim65_add_callback_range(s, 0xC000, 1024, catch_OLDADR_write, sim65_cb_write);
    poke(s, 0x2be, 64); // SHFLOK

    return 0;
}
