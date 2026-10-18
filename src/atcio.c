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
    // Uninitialized memory reads as 0x100, map to 0
    return sim65_get_byte(s, addr) & 0xFF;
}

static unsigned dpeek(sim65 s, unsigned addr)
{
    return peek(s, addr) + (peek(s, addr + 1) << 8);
}

// EDITOR defs
#define LMARGN (0x52) // left margin
#define RMARGN (0x53) // right margin
#define ROWCRS (0x54) // cursor row
#define COLCRS (0x55) // cursor column (2 byte)
// Note: CH ($2FC) is callback backed, access only via atario_ch_value()
#define DSPFLG (0x2FE) // display control chars flag
// SCREEN defs
#define ATACHR (0x2FB)
#define FILDAT (0x2FD)
#define FILFLG (0x2B7)

#define LO(a) ((a) & 0xFF)
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

// Emulated text screen, as seen by programs writing to the "E:" device.
// Stores the raw ATASCII codes, including the inverse-video bit.
#define SCR_COLS 40
#define SCR_ROWS 24
static unsigned char editr_scr[SCR_ROWS * SCR_COLS];

// State for reading an input record from the emulated screen, this
// emulates the Atari "E:" driver behavior of reading back the text
// entered by the user from screen memory.
static int editr_get_active       = 0;
static int editr_get_empty        = 0;
static unsigned editr_get_row     = 0;
static unsigned editr_get_col     = 0;
static unsigned editr_get_end_row = 0;
static unsigned editr_get_end_col = 0;

static int logmap_get(sim65 s, unsigned row);

// Find the last non-blank character in the logical line starting at the
// current cursor position.  A logical line continues while the next row's
// LOGMAP bit is clear.  The scan respects RMARGN on every physical row.
static void editr_get_begin_at(sim65 s, unsigned row, unsigned col)
{
    unsigned end_row;
    unsigned end_col;
    int found = 0;

    if (row >= SCR_ROWS)
        row = SCR_ROWS - 1;
    if (col >= SCR_COLS)
        col = SCR_COLS - 1;

    editr_get_row     = row;
    editr_get_col     = col;
    editr_get_end_row = row;
    editr_get_end_col = col;
    editr_get_empty   = 0;

    end_row = row;
    end_col = col;
    while (1)
    {
        unsigned limit = peek(s, RMARGN);
        unsigned c;
        if (limit >= SCR_COLS)
            limit = SCR_COLS - 1;
        for (c = col; c <= limit; c++)
        {
            if (atari_screen_get(row, c) != ' ')
            {
                end_row = row;
                end_col = c;
                found   = 1;
            }
        }
        if (row + 1 >= SCR_ROWS || logmap_get(s, row + 1))
            break;
        row++;
        col = peek(s, LMARGN);
        if (col >= SCR_COLS)
            break;
    }

    if (!found)
        editr_get_empty = 1;
    else
    {
        editr_get_end_row = end_row;
        editr_get_end_col = end_col;
    }
}

static void editr_get_begin(sim65 s)
{
    editr_get_begin_at(s, peek(s, ROWCRS), dpeek(s, COLCRS));
}

// Returns next character of the current input record from the screen,
// starting at the current cursor position and ending at the last
// non-blank character of the logical line.
static int editr_get_record(sim65 s)
{
    unsigned row, col;
    int c;

    if (!editr_get_active)
    {
        editr_get_begin(s);
        editr_get_active = 1;
        sim65_dprintf(s, "GETREC start (%u,%u) end=(%u,%u)%s",
                      editr_get_row, editr_get_col, editr_get_end_row,
                      editr_get_end_col, editr_get_empty ? " empty" : "");
    }
    if (editr_get_empty)
    {
        editr_get_active = 0;
        return 0x9B;
    }

    row = editr_get_row;
    col = editr_get_col;
    if (row > editr_get_end_row || (row == editr_get_end_row && col > editr_get_end_col))
    {
        editr_get_active = 0;
        return 0x9B;
    }

    c = atari_screen_get(row, col);

    col++;
    if (col > (unsigned)peek(s, RMARGN))
    {
        col = peek(s, LMARGN);
        row++;
    }
    editr_get_row = row;
    editr_get_col = col;
    return c;
}

// Returns the character at the given position of the emulated text screen.
unsigned char atari_screen_get(int row, int col)
{
    if (row < 0 || col < 0 || row >= SCR_ROWS || col >= SCR_COLS)
        return 0;
    return editr_scr[row * SCR_COLS + col];
}

// Full editor mode flag (set at boot from the atari option flags)
int atari_opt_full_editor = 1;

// ATASCII-to-internal character conversion table (ROM TAIC, line 13749)
// The ROM uses 4 ROL operations to extract bits 6-5 of the ATASCII character,
// then ORs in the corresponding bits from this table.
static const unsigned char taic_table[4] = { 0x40, 0x00, 0x20, 0x60 };

static unsigned char atascii_to_internal(unsigned char c)
{
    return (c & 0x9F) | taic_table[(c >> 5) & 3];
}

// Restore the character under the cursor, undoing the cursor glyph (bit 7).
// Mirrors the ROM ROD: if OLDADR (page > 0) is set, the character under it is
// re-synced from editr_scr, clearing any cursor glyph drawn there.
static void editr_sync_pixel(sim65 s, unsigned row, unsigned col);
static void editr_cursor_restore(sim65 s)
{
    if (!atari_opt_full_editor)
        return;
    unsigned oldaddr = dpeek(s, 0x5E);
    if (HI(oldaddr) == 0)
        return; // ROM ROD: skip while OLDADR is in page zero
    unsigned offset = oldaddr - dpeek(s, 0x58);
    editr_sync_pixel(s, offset / SCR_COLS, offset % SCR_COLS);
}

// Update the cursor to (row,col) and, if CRSINH allows, draw the cursor glyph
// (OLDCHR ^ $80) into screen RAM at the new cursor address.
// Mirrors the ROM SEC_: OLDADR/OLDCHR are always updated (via CCA/GDC); the
// inverse-video cursor is only written to screen memory when CRSINH == 0.
static void editr_cursor_update(sim65 s, unsigned row, unsigned col)
{
    if (!atari_opt_full_editor)
        return;
    if (row >= SCR_ROWS || col >= SCR_COLS)
        return;
    unsigned addr   = dpeek(s, 0x58) + row * SCR_COLS + col;
    unsigned char c = editr_scr[row * SCR_COLS + col];
    // Track the cursor position in OLDADR/OLDCHR regardless of CRSINH
    // (ROM SECS always updates OLDCHR; only the drawing is gated).
    dpoke(s, 0x5E, addr);                  // OLDADR
    poke(s, 0x5D, atascii_to_internal(c)); // OLDCHR
    // The cursor is only drawn when CRSINH is zero (ROM SECS: if the
    // cursor inhibit flag is set, skip drawing).
    if (peek(s, 0x02F0) != 0)
        return;
    // Cursor glyph is the inverse of the character under the cursor
    poke(s, addr, atascii_to_internal(c) | 0x80);
}

// Sync a single pixel from editr_scr to actual 6502 screen RAM at SAVMSC.
// editr_scr holds raw ATASCII codes; the screen RAM cell gets the internal
// glyph code. No control-char filtering here: a control char can only reach
// editr_scr via the ESC-verbatim path (where it must display its glyph).
static void editr_sync_pixel(sim65 s, unsigned row, unsigned col)
{
    if (!atari_opt_full_editor)
        return;
    unsigned addr   = dpeek(s, 0x58) + row * 40 + col;
    unsigned char c = editr_scr[row * SCR_COLS + col];
    poke(s, addr, atascii_to_internal(c));
}

// Sync an entire row from editr_scr to screen RAM.
static void editr_sync_row(sim65 s, unsigned row)
{
    for (unsigned col = 0; col < SCR_COLS; col++)
        editr_sync_pixel(s, row, col);
}

// Sync the entire screen from editr_scr to screen RAM.
static void editr_sync_all(sim65 s)
{
    for (unsigned row = 0; row < SCR_ROWS; row++)
        editr_sync_row(s, row);
}

// LOGMAP — Logical line bitmap (ROM $02B2, 3 bytes for 24 screen rows)
// Bit N = 1 → row N is the START of a logical line
// Bit N = 0 → row N is a CONTINUATION of the logical line above
// Initialized to all-ones on clear screen (every row starts a new logical line)
// Stored in emulated RAM at the ROM OS address.
#define LOGMAP (0x02B2)

// LOGCOL — Logical column position within the current logical line (ROM $0063)
// Range: 0..119 (up to 3 physical rows × 40 columns)
#define LOGCOL (0x0063)

// ESCFLG — Escape flag (ROM $02A2)
// When bit 7 is set ($80), the next character is printed verbatim
#define ESCFLG (0x02A2)

// TABMAP — Tab stop bitmap (ROM $02A3, 15 bytes = 120 bits)
// Initialized to every 8 columns
#define TABMAP (0x02A3)

// Get LOGMAP bit for row N (1 = start of logical line)
static int logmap_get(sim65 s, unsigned row)
{
    if (row >= SCR_ROWS)
        return 1;
    return (peek(s, LOGMAP + (row >> 3)) >> (row & 7)) & 1;
}

// Set LOGMAP bit for row N
static void logmap_set(sim65 s, unsigned row, int val)
{
    unsigned addr;
    if (row >= SCR_ROWS)
        return;
    addr = LOGMAP + (row >> 3);
    if (val)
        poke(s, addr, peek(s, addr) | (1 << (row & 7)));
    else
        poke(s, addr, peek(s, addr) & ~(1 << (row & 7)));
}

// Shift LOGMAP bits up by 1 (for line deletion at row 'from_row')
// The deleted row's bit is replaced by the next row's bit.
static void logmap_shift_up(sim65 s, unsigned from_row)
{
    for (unsigned r = from_row; r < SCR_ROWS - 1; r++)
        logmap_set(s, r, logmap_get(s, r + 1));
    // Bottom row always starts a new logical line
    logmap_set(s, SCR_ROWS - 1, 1);
}

// Shift LOGMAP bits down by 1 (for line insertion at row 'from_row')
// The inserted row gets bit = 1 (starts a new logical line).
static void logmap_shift_down(sim65 s, unsigned from_row)
{
    for (unsigned r = SCR_ROWS - 1; r > from_row; r--)
        logmap_set(s, r, logmap_get(s, r - 1));
    logmap_set(s, from_row, 1);
}

// Compute LOGCOL from cursor position by walking UP to find logical line start.
static unsigned compute_logcol(sim65 s, unsigned row, unsigned col)
{
    unsigned lc = col;
    int r       = (int)row;
    while (r > 0 && !logmap_get(s, r))
    {
        r--;
        lc += SCR_COLS;
    }
    return lc;
}

// Initialize LOGCOL/ESCFLG/TABMAP/LOGMAP as for a fresh screen.
static void logmap_init(sim65 s)
{
    // LOGMAP all ones: every row starts a new logical line
    for (unsigned r = 0; r < SCR_ROWS; r++)
        logmap_set(s, r, 1);
    // LOGCOL and ESCFLG reset
    poke(s, LOGCOL, 0);
    poke(s, ESCFLG, 0);
    // Tab stops cleared, then default stops every 8 columns (col%8==7,
    // matching ROM TBTM convention where bit $01 of each TABMAP byte).
    for (unsigned i = 0; i < 15; i++)
        poke(s, TABMAP + i, 0);
    for (unsigned i = 7; i < 120; i += 8)
        poke(s, TABMAP + (i >> 3), peek(s, TABMAP + (i >> 3)) | (1 << (i & 7)));
}

// Get tabmap bit for column N (1 = tab stop)
static int tabmap_get(sim65 s, unsigned col)
{
    if (col >= 120)
        return 0;
    return (peek(s, TABMAP + (col >> 3)) >> (col & 7)) & 1;
}

// Set tabmap bit for column N
static void tabmap_set(sim65 s, unsigned col, int val)
{
    unsigned addr;
    if (col >= 120)
        return;
    addr = TABMAP + (col >> 3);
    if (val)
        poke(s, addr, peek(s, addr) | (1 << (col & 7)));
    else
        poke(s, addr, peek(s, addr) & ~(1 << (col & 7)));
}

// Forward declarations for functions used by editr_ret
static void editr_scr_del_line(sim65 s, unsigned row);

// RET equivalent: Advance cursor to next row, left margin.
// If at the bottom row, scroll the whole logical line off the top,
// repeating until the top row is the start of a new logical line
// (matches the ROM's RET3/RET4 scroll loop).
static void editr_ret(sim65 s, unsigned *row, unsigned *col)
{
    *col = peek(s, LMARGN);
    if (*row < SCR_ROWS - 1)
    {
        (*row)++;
        return;
    }
    if (!atari_opt_full_editor)
    {
        editr_scr_del_line(s, 0);
        return;
    }
    unsigned scrolled = 0;
    do
    {
        editr_scr_del_line(s, 0);
        logmap_shift_up(s, 0);
        scrolled++;
    } while (!logmap_get(s, 0));
    *row = SCR_ROWS - scrolled;
}

// Returns the current E: cursor position.
void atario_cursor_get(int *row, int *col)
{
    *row = editr_last_row;
    *col = editr_last_col;
}

static void editr_scr_clear(sim65 s)
{
    memset(editr_scr, ' ', sizeof(editr_scr));
    editr_get_active = 0;
    logmap_init(s);
    editr_sync_all(s);
}

// Updates the emulated screen with a character written to "E:" at the
// given cursor position. Control characters are not stored, as they are
// handled by the caller (unless DSPFLG is set).
// Note: $00-$1A are graphical characters on the Atari, not control chars.
static void editr_scr_put(sim65 s, unsigned row, unsigned col, unsigned char c)
{
    if (row >= SCR_ROWS || col >= SCR_COLS)
        return;
    // Only filter actual control characters: $1B-$1F (cursor/escape) and $9B (EOL)
    if (!peek(s, DSPFLG) && ((c >= 0x1B && c <= 0x1F) || c == 0x9B))
        return;
    editr_scr[row * SCR_COLS + col] = c;
    editr_sync_pixel(s, row, col);
}

// Deletes the character at the given position of the given row, moving
// the rest of the row to the left (as the "delete" control character).
static void editr_scr_del(sim65 s, unsigned row, unsigned col)
{
    if (row >= SCR_ROWS || col >= SCR_COLS)
        return;
    memmove(editr_scr + row * SCR_COLS + col,
            editr_scr + row * SCR_COLS + col + 1, SCR_COLS - col - 1);
    editr_scr[row * SCR_COLS + SCR_COLS - 1] = ' ';
    editr_sync_row(s, row);
}

// Deletes the given physical line, moving the lines below up.
static void editr_scr_del_line(sim65 s, unsigned row)
{
    if (row >= SCR_ROWS)
        return;
    memmove(editr_scr + row * SCR_COLS,
            editr_scr + (row + 1) * SCR_COLS,
            (SCR_ROWS - row - 1) * SCR_COLS);
    memset(editr_scr + (SCR_ROWS - 1) * SCR_COLS, ' ', SCR_COLS);
    // Sync all affected rows
    for (unsigned r = row; r < SCR_ROWS; r++)
        editr_sync_row(s, r);
}

// Inserts a new blank physical line at the given row, moving the lines
// below down.
static void editr_scr_ins_line(sim65 s, unsigned row)
{
    if (row >= SCR_ROWS)
        return;
    memmove(editr_scr + (row + 1) * SCR_COLS,
            editr_scr + row * SCR_COLS,
            (SCR_ROWS - row - 1) * SCR_COLS);
    memset(editr_scr + row * SCR_COLS, ' ', SCR_COLS);
    // Sync all affected rows
    for (unsigned r = row; r < SCR_ROWS; r++)
        editr_sync_row(s, r);
}

// Wrap: advance the cursor to the next row (RET) and, if that row starts
// a new logical line, extend the current logical line into a new blank
// row in its place (matches the ROM's ACC4 + ILN1). The destination row
// becomes a continuation of the current logical line.
static void editr_ret_wrap(sim65 s, unsigned *row, unsigned *col)
{
    editr_ret(s, row, col);
    if (!atari_opt_full_editor || !logmap_get(s, *row))
        return;
    editr_scr_ins_line(s, *row);
    logmap_shift_down(s, *row);
    logmap_set(s, *row, 0);
}

// Full editor INSERT ($FF): Shifts characters right within the current
// logical line (up to 120 chars / 3 physical rows).
// Stops when reaching the logical line boundary (next row with LOGMAP bit SET).
static void editr_ins_logical(sim65 s, unsigned row, unsigned col)
{
    unsigned r          = row;
    unsigned c          = col;
    unsigned char saved = ' ';

    /* Shift the current logical line right by one position, starting at the
     * cursor.  Continue through continuation rows, but stop at the end of the
     * logical line or at the 3-row/120-column limit. */
    while (1)
    {
        unsigned char next          = editr_scr[r * SCR_COLS + c];
        editr_scr[r * SCR_COLS + c] = saved;
        editr_sync_pixel(s, r, c);
        saved = next;

        if (c < (unsigned)peek(s, RMARGN))
            c++;
        else
        {
            c = peek(s, LMARGN);
            r++;
            if (r >= SCR_ROWS || logmap_get(s, r))
                break;
        }

        if (compute_logcol(s, r, c) >= 120)
            break;
    }
}

// Full editor DELETE ($FE): Shifts characters left within the current
// logical line. If the entire physical line becomes empty, collapses it.
static void editr_del_logical(sim65 s, unsigned row, unsigned col)
{
    unsigned start_lc = compute_logcol(s, row, col);
    unsigned prev_row = row, prev_col = col;

    while (1)
    {
        // Advance to next position
        unsigned next_col = prev_col + 1;
        unsigned next_row = prev_row;
        if (next_col > (unsigned)peek(s, RMARGN))
        {
            next_col = peek(s, LMARGN);
            next_row = prev_row + 1;
            if (next_row >= SCR_ROWS)
                break;
            if (logmap_get(s, next_row))
                break;
        }

        unsigned next_lc = compute_logcol(s, next_row, next_col);
        if (next_lc <= start_lc)
            break;

        unsigned char next_c                      = editr_scr[next_row * SCR_COLS + next_col];
        editr_scr[prev_row * SCR_COLS + prev_col] = next_c;
        editr_sync_pixel(s, prev_row, prev_col);

        prev_row = next_row;
        prev_col = next_col;
    }

    // Clear last position
    editr_scr[prev_row * SCR_COLS + prev_col] = ' ';
    editr_sync_pixel(s, prev_row, prev_col);

    // Check if the physical line is now empty — if so, collapse it
    // (like DWQ/DLN1 in the ROM)
    unsigned line_start_col = peek(s, LMARGN);
    int empty               = 1;
    for (unsigned c = line_start_col; c <= (unsigned)peek(s, RMARGN); c++)
    {
        if (editr_scr[row * SCR_COLS + c] != ' ')
        {
            empty = 0;
            break;
        }
    }
    if (empty && row > 0 && col != line_start_col)
    {
        // Collapse: shift screen up, shift LOGMAP up
        editr_scr_del_line(s, row);
        logmap_shift_up(s, row);
    }
}

// Type a line from the keyboard into the emulated screen, processing it
// through the same full-editor path as E: PUT.  The completed line is then
// returned by editr_get_record() from the screen.
static void editr_put_char(sim65 s, unsigned char c, int echo_host);

static int editr_type_input(sim65 s)
{
    unsigned start_row = peek(s, ROWCRS);
    unsigned start_col = dpeek(s, COLCRS);
    int c;

    for (;;)
    {
        c = atari_get_char();
        if (c == EOF || c < 0)
            return 0;
        editr_put_char(s, (unsigned char)c, 0);
        if (c == 0x9B)
            break;
    }

    editr_get_begin_at(s, start_row, start_col);
    editr_get_active = 1;
    return 1;
}

// Simplified stream mode for the E: device: no screen contents are emulated.
static void editr_put_stream(sim65 s, unsigned char c)
{
    unsigned row = peek(s, ROWCRS);
    unsigned col = dpeek(s, COLCRS);
    atari_put_char(c);
    if (c == 0x9B) // EOL: next row, left margin
    {
        col = peek(s, LMARGN);
        if (row < SCR_ROWS - 1)
            row++;
    }
    else if (c == 0x7D) // clear screen: home the cursor
    {
        row = 0;
        col = peek(s, LMARGN);
    }
    else if (c == 0x7E) // backspace
    {
        if (col > peek(s, LMARGN))
            col--;
    }
    else if (c >= 0x1C && c <= 0x1F) // cursor arrows
    {
        if (c == 0x1C && row > 0)
            row--;
        else if (c == 0x1D && row < SCR_ROWS - 1)
            row++;
        else if (c == 0x1E && col > peek(s, LMARGN))
            col--;
        else if (c == 0x1F && col < peek(s, RMARGN))
            col++;
    }
    else if ((c & 0x7F) < 0x1C || ((c & 0x7F) >= 0x20 && (c & 0x7F) <= 0x7E))
    {
        // Graphical ($00-$1B) and printable ($20-$7E) characters
        // advance the cursor (mask bit 7: inverse ATASCII).
        col++;
        if (col > peek(s, RMARGN))
        {
            col = peek(s, LMARGN);
            if (row < SCR_ROWS - 1)
                row++;
        }
    }
    // Remaining control codes are ignored (with no advance)
    poke(s, ROWCRS, row);
    dpoke(s, COLCRS, col);
    editr_last_row = row;
    editr_last_col = col;
}

// Process one byte through the full E: editor and update the cursor state.
// echo_host controls the optional host-output stream; keyboard input uses 0.
static void editr_put_char(sim65 s, unsigned char c, int echo_host)
{
    unsigned row = peek(s, ROWCRS);
    unsigned col = dpeek(s, COLCRS);
    editr_cursor_restore(s);
    // Special characters are consumed by the editor without
    // advancing the cursor (unless stated otherwise).
    int advance = 1;
    // Detect POS changes
    if (row != editr_last_row || col != editr_last_col)
    {
        if (row != editr_last_row && echo_host)
            atari_put_char(0x9B);
        sim65_dprintf(s, "EDITR position from (%d,%d) to (%d,%d)",
                      editr_last_row, editr_last_col, row, col);
    }
    if (!peek(s, DSPFLG))
    {
        // Special characters are interpreted by the editor,
        // all other characters are printed and advance the cursor.
        if (echo_host) atari_put_char(c);

        // ESC handling: when ESCFLG is set, the next character is
        // printed verbatim (no control char interpretation).  EOL is
        // the exception: it cannot be escaped and does not clear
        // ESCFLG (ATARI-E.md).
        if ((peek(s, ESCFLG) & 0x80) && c != 0x9B)
        {
            poke(s, ESCFLG, 0);
            // Print character verbatim (no control char interpretation).
            // The raw ATASCII char is kept in editr_scr; the screen
            // RAM cell gets its internal glyph even for $1B-$1F
            // (bypassing the control-char filter in editr_sync_pixel).
            editr_scr[row * SCR_COLS + col] = c;
            editr_sync_pixel(s, row, col);
            if (col >= peek(s, RMARGN))
            {
                editr_ret_wrap(s, &row, &col);
                advance = 0;
            }
        }
        else
            switch (c)
            {
                case 0x9B: // end of line
                    // EOL starts a new logical line (not extending).
                    // Cursor stays at the left margin (no advance).
                    // EOL cannot be escaped; it also ends the escape.
                    poke(s, ESCFLG, 0);
                    editr_ret(s, &row, &col);
                    advance = 0;
                    break;
                case 0x7D: // clear screen and home the cursor
                    // The real hardware clears the screen and puts
                    // the cursor in the upper left corner (left
                    // margin, top row).
                    editr_scr_clear(s);
                    row = 0;
                    col = peek(s, LMARGN);
                    // LOGCOL resets with the cursor position (CHM).
                    poke(s, LOGCOL, peek(s, LMARGN));
                    advance = 0;
                    break;
                case 0x7E: // backspace: delete char left of cursor
                    if (col > peek(s, LMARGN))
                    {
                        col--;
                        editr_scr_del(s, row, col);
                    }
                    advance = 0;
                    break;
                case 0x1C: // move cursor up
                    advance = 0;
                    if (row > 0)
                        row--;
                    else
                        row = SCR_ROWS - 1;
                    break;
                case 0x1D: // move cursor down
                    advance = 0;
                    if (row < SCR_ROWS - 1)
                        row++;
                    else
                        row = 0;
                    break;
                case 0x1E: // move cursor left
                    advance = 0;
                    if (col > peek(s, LMARGN))
                        col--;
                    else
                        col = peek(s, RMARGN);
                    break;
                case 0x1F: // move cursor right
                    advance = 0;
                    if (col < peek(s, RMARGN))
                        col++;
                    else
                        col = peek(s, LMARGN);
                    break;
                case 0x1B: // ESC: set ESCFLG, next char printed verbatim
                    poke(s, ESCFLG, 0x80);
                    advance = 0;
                    break;
                case 0x7F: // TAB: move cursor to next tab stop
                {
                    unsigned next_tab = col + 1;
                    while (next_tab < SCR_COLS && !tabmap_get(s, next_tab))
                        next_tab++;
                    if (next_tab < SCR_COLS)
                        col = next_tab;
                    advance = 0;
                    break;
                }
                case 0x9E: // CLEAR TAB at cursor column
                    tabmap_set(s, col, 0);
                    advance = 0;
                    break;
                case 0x9F: // SET TAB at cursor column
                    tabmap_set(s, col, 1);
                    advance = 0;
                    break;
                case 0x9C: // delete line
                    // Delete the whole logical line: starting at the
                    // row where it begins, delete continuation rows
                    // until the (new) current row starts a new
                    // logical line (matches the ROM's DLN loop).
                    while (row > 0 && !logmap_get(s, row))
                        row--;
                    do
                    {
                        editr_scr_del_line(s, row);
                        logmap_shift_up(s, row);
                    } while (!logmap_get(s, row));
                    col     = peek(s, LMARGN);
                    advance = 0;
                    break;
                case 0x9D: // insert line
                    editr_scr_ins_line(s, row);
                    logmap_shift_down(s, row);
                    advance = 0;
                    break;
                case 0xFE: // delete character at cursor
                    editr_del_logical(s, row, col);
                    advance = 0;
                    break;
                case 0xFF: // insert character at cursor
                    editr_ins_logical(s, row, col);
                    advance = 0;
                    break;
                default:
                    // Bell ($FD): no-op for simulation
                    // $00-$1A are graphical chars (displayed).
                    // $9B is EOL (consumed, not displayed).
                    // $1B-$1F are control chars (consumed, not displayed).
                    if (c != 0xFD && c != 0x9B && !(c >= 0x1C && c <= 0x1F))
                    {
                        editr_scr_put(s, row, col, c);
                        if (col >= peek(s, RMARGN))
                        {
                            // Wrap: character extends the logical line
                            editr_ret_wrap(s, &row, &col);
                            advance = 0;
                        }
                    }
                    else
                        advance = 0;
                    break;
            }
    }
    else
    {
        // DSPFLG set: print all characters verbatim
        if (echo_host) atari_put_char(c);
        if (c == 0x9B)
        {
            col = peek(s, LMARGN);
            if (row < SCR_ROWS - 1)
                row++;
            else
                editr_scr_del_line(s, 0);
        }
        else
        {
            editr_scr_put(s, row, col, c);
            if (col >= peek(s, RMARGN))
            {
                col = peek(s, LMARGN);
                if (row < SCR_ROWS - 1)
                    row++;
                else
                    editr_scr_del_line(s, 0);
            }
            else
                col++;
        }
        advance = 0; // already handled above
    }
    if (advance)
        col++;
    dpoke(s, COLCRS, col);
    poke(s, ROWCRS, row);
    editr_last_row = row;
    editr_last_col = col;
    editr_cursor_update(s, row, col);
}

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
            // GET RECORD can read back the screen (pending RETURN or forced
            // read), or collect a new line from the keyboard first.  Other GET
            // commands use the keyboard directly.
            int c;
            int com            = peek(s, ICCOMZ);
            int full           = atari_opt_full_editor;
            int get_record     = com == 4 || com == 5;
            int forced         = peek(s, ICAX1Z) & 0x01;
            int pending_return = atario_ch_value(s) == 12;

            if (full && get_record && !editr_get_active && !forced && !pending_return)
            {
                // Wait for keyboard input and echo it to the screen.
                editr_type_input(s);
            }

            if (full && get_record && (editr_get_active || forced || pending_return))
            {
                if (!editr_get_active)
                    atario_ch_clear(s);
                c = editr_get_record(s);
            }
            else
                c = atari_get_char();

            regs->y = 1;
            if (c == EOF || c < 0)
                regs->y = 136;
            else
                regs->a = c;
            return 0;
        }
        case DEVR_PUT:
            if (!atari_opt_full_editor)
                editr_put_stream(s, regs->a);
            else
                editr_put_char(s, regs->a, 1);
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
    return 0;
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
    editr_scr_clear(s);

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
