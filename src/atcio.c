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
#include "atari.h"
#include "ataridos.h"
#include "ciodev.h"
#include <stdarg.h>
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
        atari_put_char(0xFF & (*p));
    return size;
}

// Screen routines callback
enum scr_command
{
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
    static uint8_t scr[320 * 200]; // Simulated screen
    static const int gr_sx[]   = { 40, 20, 20, 40, 80, 80, 160, 160, 320, 80, 80, 80, 40, 40, 160, 160 };
    static const int gr_sy[]   = { 24, 24, 12, 24, 48, 48, 96, 96, 192, 192, 192, 192, 24, 12, 192, 192 };
    static const int gr_numc[] = { 256, 256, 256, 4, 2, 4, 2, 4, 2, 16, 16, 16, 256, 256, 2, 4 };

    switch (cmd)
    {
        case scr_cmd_graphics:
            sim65_dprintf(s, "SCREEN: open mode %d", 0x10 ^ data);
            atari_printf("SCREEN: set graphics %d%s%s\n", data & 15,
                         data & 16 ? " with text window" : "",
                         data & 32 ? " don't clear" : "");
            if (0 == (data & 32))
                memset(scr, 0, 320 * 200);
            sx   = gr_sx[data & 15];
            sy   = gr_sy[data & 15];
            numc = gr_numc[data & 15];
            r->y = 0;
            return;
        case scr_cmd_locate:
            sim65_dprintf(s, "SCREEN: get (locate) @(%d, %d)", x, y);
            atari_printf("SCREEN: locate %d,%d\n", x, y);
            if (x >= 0 && x < sx && y >= 0 && y < sy)
                r->a = scr[y * 320 + x];
            break;
        case scr_cmd_plot:
            sim65_dprintf(s, "SCREEN: put (plot) @(%d, %d) color: %d", x, y, data);
            atari_printf("SCREEN: plot %d,%d  color %d\n", x, y, data % numc);
            if (x >= 0 && x < sx && y >= 0 && y < sy)
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
                         x, y, (data & 0xFF) % numc, (data >> 8) % numc);
            break;
    }
    if (x < 0 || x >= sx || y < 0 || y >= sy)
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

static const unsigned char hatab_default[] = {
    HATABS_ENTRY('P', PRINTV),
    HATABS_ENTRY('C', CASETV),
    HATABS_ENTRY('E', EDITRV), // NOTE: Editor must be entry at offset "6"
    HATABS_ENTRY('S', SCRENV),
    HATABS_ENTRY('K', KEYBDV),
    HATABS_ENTRY(0, 0),
    HATABS_ENTRY(0, 0),
    HATABS_ENTRY(0, 0),
    HATABS_ENTRY(0, 0),
    HATABS_ENTRY(0, 0),
    HATABS_ENTRY(0, 0)
};

// Standard Handlers
static const unsigned char devhand_tables[] = {
    DEVH_TAB(EDITR),
    DEVH_TAB(SCREN),
    DEVH_TAB(KEYBD),
    DEVH_TAB(PRINT),
    DEVH_TAB(CASET)
};

// IOCB defs
#define CIOV   (0xE456)
#define CIOERR (0xE530) // FIXME: CIO error return routine
#define ZIOCB  (0x20)   // ; ZP copy of IOCB

static const unsigned char iocv_empty[16] = {
    0xFF, 0, 0, 0,                        // HID, DNO, COM, STA
    0, 0, LO(CIOERR - 1), HI(CIOERR - 1), // BAL, BAH, PTL, PTH
    0, 0, 0, 0,                           // BLL, BLH, AX1, AX2
    0, 0, 0, 0                            // AX3, AX4, AX5, SPR
};

const char *cio_cmd_name(int cmd)
{
    struct
    {
        int num;
        const char *name;
    } cmds[] = {
        { 3, "OPEN" },
        { 5, "GETREC" },
        { 7, "GETCHR" },
        { 9, "PUTREC" },
        { 11, "PUTCHR" },
        { 12, "CLOSE" },
        { 13, "STATUS" },
        { 17, "DRAWLN" },
        { 18, "FILLIN" },
        { 0x20, "RENAME" },
        { 0x21, "DELETE" },
        { 0x23, "LOCKFL" },
        { 0x24, "UNLOCK" },
        { 0x25, "POINT" },
        { 0x26, "NOTE" },
        { 0x27, "GETFL" },
        { 0x28, "LOAD" },
        { 0, 0 }
    };

    if (cmd < 3)
        return "invalid";

    for (int i = 0; cmds[i].num; i++)
        if (cmds[i].num == cmd)
            return cmds[i].name;
    return "special";
}

static int cio_store(sim65 s, struct sim65_reg *regs)
{
    int inum = peek(s, ICIDNO);
    // Restore X from ICIDNO
    regs->x = inum;
    // Store status from Y
    poke(s, ICSTAZ, regs->y);
    // Copy ZIOCB back
    for (int i = 0; i < 12; i++)
        poke(s, IOCB + inum + i, peek(s, ZIOCB + i));
    // Set flags
    if (regs->y & 0x80)
        sim65_set_flags(s, SIM65_FLAG_N, SIM65_FLAG_N);
    else
        sim65_set_flags(s, SIM65_FLAG_N, 0);
    return 0;
}

static int cio_error(sim65 s, struct sim65_reg *regs, const char *err, unsigned value)
{
    regs->y = value & 0xFF;
    sim65_set_flags(s, SIM65_FLAG_N, SIM65_FLAG_N);
    sim65_dprintf(s, "CIO: %s", err);
    return 1;
}

// Calls through DEVTAB offset
static void call_devtab(sim65 s, struct sim65_reg *regs, int fn)
{
    // Get device table
    unsigned hid    = peek(s, ICHIDZ);
    unsigned devtab = dpeek(s, 1 + hid + HATABS);

    if (fn == DEVR_PUT)
        poke(s, CIOCHR, regs->a);

    unsigned addr = dpeek(s, devtab + 2 * fn);
    dpoke(s, ICSPRZ, addr);
    regs->x = peek(s, ICIDNO);
    sim65_call(s, regs, 1 + addr);

    if (fn == DEVR_GET)
        poke(s, CIOCHR, regs->a);
}

static const char *cio_fname(sim65 s)
{
    static char buf[48];
    unsigned adr = dpeek(s, ICBALZ);
    int i;
    for (i = 0; i < 47; i++)
    {
        uint8_t c = peek(s, adr + i);
        if (c < '!' || c > 'z')
            break;
        buf[i] = c;
    }
    buf[i] = 0;
    return buf;
}

// Performs CIO part of open: search HATAB and init IOCBZ
static int cio_init_open(sim65 s, struct sim65_reg *regs)
{
    // Get handle and call open routine.
    unsigned badr = dpeek(s, ICBALZ);
    unsigned dev  = peek(s, badr);
    unsigned num  = peek(s, badr + 1) - '0';

    sim65_dprintf(s, "CIO open '%s'", cio_fname(s));

    if (num > 9 || num < 1)
        num = 1;

    // Search HATAB
    unsigned hid = 0xFF;
    for (int i = 0; i < 32; i += 3)
    {
        if (peek(s, HATABS + i) == dev)
            hid = i;
    }

    // Error if not found
    if (hid == 0xFF)
        return cio_error(s, regs, "invalid device name", 0x82);

    // Get devtab
    unsigned devtab = dpeek(s, 1 + hid + HATABS);

    // Store into IOCBZ
    poke(s, ICHIDZ, hid);
    dpoke(s, ICPTLZ, dpeek(s, devtab + 6));
    poke(s, ICDNOZ, num);
    return 0;
}

static void cio_fix_length(sim65 s, struct sim65_reg *regs)
{
    regs->x = peek(s, ICIDNO);
    dpoke(s, ICBLLZ, dpeek(s, regs->x + ICBLL) - dpeek(s, ICBLLZ));
    dpoke(s, ICBALZ, dpeek(s, regs->x + ICBAL));
}

static int cio_do_command(sim65 s, struct sim65_reg *regs)
{
    unsigned com = peek(s, ICCOMZ);
    unsigned ax1 = peek(s, ICAX1Z);

    // Assume no error
    regs->y = 1;

    if ((com >= 4) && (com < 8) && !(ax1 & 0x4))
        return cio_error(s, regs, "write only", 131);

    if ((com >= 8) && (com < 12) && !(ax1 & 0x8))
        return cio_error(s, regs, "read only", 135);

    // Commands
    if (com == 4 || com == 5)
    {
        // GET RECORD
        int long_record = 0;
        for (;;)
        {
            // Get single
            call_devtab(s, regs, DEVR_GET);
            if (regs->y & 0x80)
                break;
            if (dpeek(s, ICBLLZ))
            {
                poke(s, dpeek(s, ICBALZ), regs->a);
                dpoke(s, ICBALZ, dpeek(s, ICBALZ) + 1);
                dpoke(s, ICBLLZ, dpeek(s, ICBLLZ) - 1);
            }
            else
                long_record = 1;

            if (regs->a == 0x9B)
                break;
        }
        if (long_record)
            regs->y = 0x89;
        cio_fix_length(s, regs);
    }
    else if (com == 6 || com == 7)
    {
        // GET CHARS
        if (!dpeek(s, ICBLLZ))
        {
            // Get single
            call_devtab(s, regs, DEVR_GET);
        }
        else
        {
            while (dpeek(s, ICBLLZ))
            {
                // get
                call_devtab(s, regs, DEVR_GET);
                if (regs->y & 0x80)
                    break;
                poke(s, dpeek(s, ICBALZ), regs->a);
                dpoke(s, ICBALZ, dpeek(s, ICBALZ) + 1);
                dpoke(s, ICBLLZ, dpeek(s, ICBLLZ) - 1);
            }
            cio_fix_length(s, regs);
        }
    }
    else if (com == 8 || com == 9)
    {
        // PUT RECORD
        if (!dpeek(s, ICBLLZ))
        {
            // Put single
            call_devtab(s, regs, DEVR_PUT);
        }
        else
        {
            while (dpeek(s, ICBLLZ))
            {
                regs->a = peek(s, dpeek(s, ICBALZ));
                call_devtab(s, regs, DEVR_PUT);
                if (regs->y & 0x80)
                    break;
                dpoke(s, ICBALZ, dpeek(s, ICBALZ) + 1);
                dpoke(s, ICBLLZ, dpeek(s, ICBLLZ) - 1);
                if (peek(s, CIOCHR) == 0x9B)
                    break;
            }
            // Put extra EOL if wrote all length
            if (!dpeek(s, ICBLLZ))
            {
                regs->a = 0x9B;
                call_devtab(s, regs, DEVR_PUT);
            }
            cio_fix_length(s, regs);
        }
    }
    else if (com == 10 || com == 11)
    {
        // PUT CHARS
        if (!dpeek(s, ICBLLZ))
        {
            // Put single
            call_devtab(s, regs, DEVR_PUT);
        }
        else
        {
            while (dpeek(s, ICBLLZ))
            {
                regs->a = peek(s, dpeek(s, ICBALZ));
                call_devtab(s, regs, DEVR_PUT);
                if (regs->y & 0x80)
                    break;
                dpoke(s, ICBALZ, dpeek(s, ICBALZ) + 1);
                dpoke(s, ICBLLZ, dpeek(s, ICBLLZ) - 1);
            }
            cio_fix_length(s, regs);
        }
    }
    else if (com == 12)
    {
        // CLOSE
        // Call close handler
        call_devtab(s, regs, DEVR_CLOSE);
        poke(s, ICHIDZ, 0xFF);
        dpoke(s, ICPTLZ, CIOERR - 1);
    }
    else if (com == 13)
    {
        // GET STATUS
        call_devtab(s, regs, DEVR_STATUS);
    }
    else if (com >= 14)
    {
        // SPECIAL
        call_devtab(s, regs, DEVR_SPECIAL);
    }
    return 0;
}

static void cio_clear_iocb(sim65 s)
{
    for (int i = 0; i < 8; i++)
        sim65_add_data_ram(s, IOCB + i * 16, iocv_empty, 16);
}

static int sim_CIOINV(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    cio_clear_iocb(s);
    return 0;
}

static int sim_CIOV(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    if (regs->x & 0x0F || regs->x >= 0x80)
        return cio_error(s, regs, "invalid value of X register", 134);

    // Start with cleared error:
    regs->y = 1;
    sim65_set_flags(s, SIM65_FLAG_N, 0);

    // Load from CIO and copy to ZP
    poke(s, ICIDNO, regs->x);
    for (int i = 0; i < 12; i++)
        poke(s, ZIOCB + i, peek(s, regs->x + IOCB + i));

    unsigned hid = peek(s, ICHIDZ);
    unsigned com = peek(s, ICCOMZ);

    sim65_dprintf(s, "CIO #$%02x (%02x), $%02x (%s), $%04x $%04x", regs->x, hid,
                  com, cio_cmd_name(com), dpeek(s, ICBALZ), dpeek(s, ICBLLZ));

    // Error out on invalid command
    if (com < 3)
    {
        sim65_dprintf(s, "CIO bad CMD = %d", com);
        return cio_error(s, regs, "invalid command", 132);
    }
    else if (com == 3)
    {
        // OPEN (command 0)
        if (hid != 0xFF)
            return cio_error(s, regs, "channel already opened", 129);

        if (!cio_init_open(s, regs))
            // Found, call open
            call_devtab(s, regs, DEVR_OPEN);
        cio_store(s, regs);
        return 0;
    }

    if (hid == 0xFF)
    {
        // CIO channel not open
        if (com == 12)
            // Already closed,
            return 0;
        else if (com < 12)
            // Invalid function
            return cio_error(s, regs, "channel not open", 133);
        else
        {
            // Perform "soft open", returns without restoring IOCB
            if (!cio_init_open(s, regs))
            {
                // Found, call open
                if (com == 13)
                    // GET STATUS
                    call_devtab(s, regs, DEVR_STATUS);
                else
                    // SPECIAL
                    call_devtab(s, regs, DEVR_SPECIAL);
            }
            // Copy Y to ICSTA
            poke(s, regs->x + ICSTA, regs->y);
            return 0;
        }
    }

    cio_do_command(s, regs);
    cio_store(s, regs);

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
                if (row < 24)
                    row++;
            }
            atari_put_char(regs->a);
            col++;
            dpoke(s, COLCRS, col);
            poke(s, ROWCRS, row);
            editr_last_row = row;
            editr_last_col = col;
            regs->y        = 1;
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

static int sim_screen_sms(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    // Copy SAVMSC to ADDRESS
    dpoke(s, 0x64, dpeek(s, 0x58));
    return 0;
}

static int sim_screen_drw(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    sys_screen(s, peek(s, FILFLG) ? scr_cmd_fillto : scr_cmd_drawto,
               dpeek(s, COLCRS), peek(s, ROWCRS),
               peek(s, ATACHR) | (peek(s, FILDAT) << 8), regs);
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
    unsigned cmd = peek(s, ICCOMZ);
    switch (addr & 7)
    {
        case DEVR_OPEN:
            regs->a = (peek(s, ICAX2Z) & 0x0F) | (peek(s, ICAX1Z) & 0xF0);
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

int atari_cio_add_hatab(sim65 s, char name, uint16_t address)
{
    // Search an empty entry
    for (uint16_t i = HATABS; i < IOCB; i += 3)
    {
        if (peek(s, i) == 0)
        {
            // Install our entry
            poke(s, i, name);
            dpoke(s, i + 1, address);
            return 0;
        }
    }
    return 1;
}

void atari_cio_init(sim65 s, int emu_dos)
{
    // Static variables
    editr_last_row = 0;

    // CIOV
    add_rts_callback(s, CIOV, 1, sim_CIOV);
    // CIOINV
    add_rts_callback(s, 0xE46E, 1, sim_CIOINV);
    // Init empty
    cio_clear_iocb(s);
    // Copy HTAB table
    sim65_add_data_ram(s, HATABS, hatab_default, sizeof(hatab_default));
    // Copy device handlers table
    sim65_add_data_rom(s, EDITRV, devhand_tables, sizeof(devhand_tables));
    if (emu_dos)
        atari_dos_init(s);
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
    add_rts_callback(s, CIOERR, 1, sim_CIOERR);
    // Simulate direct calls to XL OS ROM
    add_rts_callback(s, 0xEF9C, 1, sim_screen_opn);
    add_rts_callback(s, 0xF18F, 1, sim_screen_lct);
    add_rts_callback(s, 0xF1D8, 1, sim_screen_plt);
    add_rts_callback(s, 0xF9A6, 1, sim_screen_sms);
    add_rts_callback(s, 0xF9C2, 1, sim_screen_drw);
    // Random OS addresses
    poke(s, LMARGN, 2);  // LMARGIN
    poke(s, RMARGN, 39); // RMARGIN
    poke(s, ROWCRS, 0);  // ROWCRS
    dpoke(s, COLCRS, 2); // COLCRS
    poke(s, 0x57, 0);    // DINDEX
}
