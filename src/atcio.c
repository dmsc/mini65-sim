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

/* Implements Atari CIO emulation */
#include "atcio.h"
#include "dosfname.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Define callback functions
int (*atari_get_char)(void);
int (*atari_peek_char)(void);
void (*atari_put_char)(int);

// Standard put/get character
static int atari_printf(const char *format, ...)
{
    char buf[256];
    int size;
    va_list ap;
    va_start(ap, format);
    size = vsnprintf(buf, 255, format, ap);
    va_end(ap);
    for (char *p = buf; *p; p++)
        atari_put_char( 0xFF & (*p) );
    return size;
}

// Screen routines callback
enum scr_command {
    scr_cmd_graphics,
    scr_cmd_locate,
    scr_cmd_plot,
    scr_cmd_drawto,
    scr_cmd_fillto
};

static void sys_screen(sim65 s, enum scr_command cmd, int x, int y, int data,
                       struct sim65_reg *r)
{
    static int sx = 40, sy = 24, numc = 256;
    static uint8_t scr[320*200]; // Simulated screen
    static const int gr_sx[] = { 40, 20, 20, 40, 80, 80, 160, 160,
                                320, 80, 80, 80, 40, 40, 160, 160 };
    static const int gr_sy[] = { 24,  24,  12,  24, 48, 48,  96,  96,
                                192, 192, 192, 192, 24, 12, 192, 192 };
    static const int gr_numc[] = { 256, 256, 256,  4,   2,   4, 2, 4,
                                     2,  16,  16, 16, 256, 256, 2, 4 };

    switch (cmd)
    {
        case scr_cmd_graphics:
            sim65_dprintf(s, "SCREEN: open mode %d", 0x10 ^ data);
            atari_printf("SCREEN: set graphics %d%s%s\n", data & 15,
                         data & 16 ? " with text window": "",
                         data & 32 ? " don't clear" : "" );
            if (0 == (data & 32))
                memset(scr, 0, 320*200);
            sx   = gr_sx[data & 15];
            sy   = gr_sy[data & 15];
            numc = gr_numc[data & 15];
            r->y = 0;
            return;
        case scr_cmd_locate:
            sim65_dprintf(s, "SCREEN: get (locate) @(%d, %d)", x, y);
            atari_printf("SCREEN: locate %d,%d\n", x, y);
            if ( x >= 0 && x < sx && y >= 0 && y < sx )
                r->a = scr[y * 320 + x];
            break;
        case scr_cmd_plot:
            sim65_dprintf(s, "SCREEN: put (plot) @(%d, %d) color: %d", x, y, data);
            atari_printf("SCREEN: plot %d,%d  color %d\n", x, y, data % numc);
            if ( x >= 0 && x < sx && y >= 0 && y < sx )
                scr[y * 320 + x] = data % numc;
            break;
        case scr_cmd_drawto:
            // TODO: emulate line draw
            data &= 0xFF;
            sim65_dprintf(s, "SCREEN: special (drawto) @(%d, %d) color: %d", x, y, data);
            atari_printf("SCREEN: draw to %d,%d  color %d\n", x, y, data % numc);
            break;
        case scr_cmd_fillto:
            // TODO: emulate line fill
            sim65_dprintf(s, "SCREEN: special (fillto) @(%d, %d) color: %d  fcolor:%d",
                          x, y, data & 0xFF, data >> 8);
            atari_printf("SCREEN: fill to %d,%d  color %d, fill color %d\n",
                    x, y, (data & 0xFF) % numc, (data >>8) % numc );
            break;
    }
    if (x < 0 || x >= sx || y < 0 || y >= sy )
        r->y = -1;
    else
        r->y = 0;
}

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

// EDITOR defs
#define LMARGN (0x52) // left margin
#define RMARGN (0x53) // right margin
#define ROWCRS (0x54) // cursor row
#define COLCRS (0x55) // cursor column (2 byte)
// SCREEN defs
#define ATACHR (0x2FB)
#define FILDAT (0x2FD)
#define FILFLG (0x2B7)

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
#define DEVR_OPEN (0)           // 3
#define DEVR_CLOSE (1)          // 12
#define DEVR_GET (2)            // 4,5,6,7
#define DEVR_PUT (3)            // 8.9,10,11
#define DEVR_STATUS (4)         // 13
#define DEVR_SPECIAL (5)        // 14 and up
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
    if (regs->x & 0x0F || regs->x >= 0x80)
        return cio_error(s, regs, "invalid value of X register", 134);

    unsigned hid  = GET_IC(HID);
    unsigned badr = GET_IC(BAL) | (GET_IC(BAH) << 8);
    unsigned blen = GET_IC(BLL) | (GET_IC(BLH) << 8);
    unsigned com  = GET_IC(COM);
    unsigned ax1  = GET_IC(AX1);

    unsigned devtab = dpeek(s, 1 + hid + HATABS);

    if (hid == 0xFF && com != 3 && com < 12)
        return cio_error(s, regs, "channel not open", 133);

    if (com < 3)
    {
        sim65_dprintf(s, "CIO CMD = %d", com);
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
        regs->y = 1;
        if (GET_IC(HID) != 0xFF)
        {
            // Call close handler
            call_devtab(s, regs, devtab, DEVR_CLOSE);
        }
        poke(s, IC(HID), 0xFF);
        dpoke(s, IC(PTL), CIOERR - 1);
        return cio_exit(s, regs);
    }
    else if (com == 13)
    {
        // GET STATUS
    }
    else if (com >= 14)
    {
        // SPECIAL
        if (com == 37)
        {
            unsigned ax3 = GET_IC(AX3);
            unsigned ax4 = GET_IC(AX4);
            unsigned ax5 = GET_IC(AX5);
            sim65_dprintf(s, "POINT %d/%d/%d", ax3, ax4, ax5);
            return cio_ok(s, regs, 0);
        }
        // Call close handler
        call_devtab(s, regs, devtab, DEVR_SPECIAL);
        return cio_exit(s, regs);
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

static unsigned editr_last_row = 0;
static unsigned editr_last_col = 0;
static int sim_EDITR(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    switch (addr & 7)
    {
        case DEVR_OPEN:
            sim65_dprintf(s, "EDITR cmd OPEN");
            return 0;
        case DEVR_CLOSE:
            return 0;
        case DEVR_GET:
        {
            int c   = atari_get_char();
            regs->y = 1;
            if (c == EOF)
                regs->y = 136;
            else
                regs->a = c;
            return 0;
        }
        case DEVR_PUT:
        {
            unsigned row = peek(s, ROWCRS);
            unsigned col = dpeek(s, COLCRS);
            // Detect POS changes
            if (row != editr_last_row || col != editr_last_col)
            {
                if (row != editr_last_row)
                    atari_put_char(0x9B);
                sim65_dprintf(s, "EDITR position from (%d,%d) to (%d,%d)",
                              editr_last_row, editr_last_col, row, col);
            }
            if (regs->a == 0x9B || col == dpeek(s, RMARGN))
            {
                col = peek(s, LMARGN) - 1;
                if( row < 24 )
                    row ++;
            }
            atari_put_char(regs->a);
            col ++;
            dpoke(s, COLCRS, col);
            poke(s, ROWCRS, row);
            editr_last_row = row;
            editr_last_col = col;
            regs->y = 1;
            return 0;
        }
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

static int sim_screen_opn(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    sys_screen(s, scr_cmd_graphics, 0, 0, regs->a, regs);
    return 0;
}

static int sim_screen_plt(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    sys_screen(s, scr_cmd_plot, dpeek(s, COLCRS), peek(s, ROWCRS), peek(s, ATACHR), regs);
    return 0;
}

static int sim_screen_drw(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    sys_screen(s, peek(s, FILFLG) ? scr_cmd_fillto : scr_cmd_drawto,
               dpeek(s, COLCRS), peek(s, ROWCRS),
               peek(s, ATACHR) | (peek(s, FILDAT)<<8), regs);
    return 0;
}

static int sim_screen_lct(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    sys_screen(s, scr_cmd_locate, dpeek(s, COLCRS), peek(s, ROWCRS), 0, regs);
    return 0;
}

static int sim_SCREN(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    // We need IOCB data
    unsigned cmd  = GET_IC(COM);
    switch (addr & 7)
    {
        case DEVR_OPEN:
            regs->a = (GET_IC(AX2) & 0x0F) | (GET_IC(AX1) & 0xF0);
            return sim_screen_opn(s, regs, addr, data);
        case DEVR_CLOSE:
            sim65_dprintf(s, "SCREEN: close");
            return 0; // OK
        case DEVR_GET:
            return sim_screen_lct(s, regs, addr, data);
        case DEVR_PUT:
            poke(s, ATACHR, regs->a);
            return sim_screen_plt(s, regs, addr, data);
        case DEVR_STATUS:
            sim65_dprintf(s, "SCREEN: cmd STATUS");
            return 0;
        case DEVR_SPECIAL:
            if (cmd == 17 || cmd == 18)
            {
                poke(s, FILFLG, (cmd == 18));
                return sim_screen_drw(s, regs, addr, data);
            }
            else
                sim65_dprintf(s, "SCREEN: special (unknown=%d)", cmd);
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
            int c   = atari_get_char();
            regs->y = 1;
            if (c == EOF)
                regs->y = 136;
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
            fhand[chn] = dosfopen(fname, flags);
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

void atari_cio_init(sim65 s)
{
    unsigned i;
    // Static variables
    editr_last_row = 0;

    // CIOV
    add_rts_callback(s, CIOV, 1, sim_CIOV);
    // Init empty
    for (i = 0; i < 8; i++)
        sim65_add_data_ram(s, ICHID + i * 16, iocv_empty, 16);
    // Copy HTAB table
    sim65_add_data_ram(s, HATABS, hatab_default, sizeof(hatab_default));
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
    // Simulate direct calls to XL OS ROM
    add_rts_callback(s, 0xEF9C, 1, sim_screen_opn);
    add_rts_callback(s, 0xF18F, 1, sim_screen_lct);
    add_rts_callback(s, 0xF1D8, 1, sim_screen_plt);
    add_rts_callback(s, 0xF9C2, 1, sim_screen_drw);
    // Random OS addresses
    poke(s, LMARGN, 2); // LMARGIN
    poke(s, RMARGN, 39); // RMARGIN
    poke(s, ROWCRS, 0); // ROWCRS
    dpoke(s, COLCRS, 2); // COLCRS
    poke(s, 0x57, 0); // DINDEX
}

