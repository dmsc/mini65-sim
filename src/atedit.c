/*
 * Mini65 - Small 6502 simulator with Atari 8bit bios.
 *
 * Copyright (C) 2017-2026 Daniel Serpell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Implements the Atari E: editor device. */
#include "atedit.h"
#include "atari.h"
#include "atari_mem.h"
#include "atcio.h"
#include "ciodev.h"
#include <stdio.h>
#include <string.h>

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
    unsigned oldaddr = dpeek(s, OLDADR);
    if (HI(oldaddr) == 0)
        return; // ROM ROD: skip while OLDADR is in page zero
    unsigned offset = oldaddr - dpeek(s, SAVMSC);
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
    unsigned addr   = dpeek(s, SAVMSC) + row * SCR_COLS + col;
    unsigned char c = editr_scr[row * SCR_COLS + col];
    // Track the cursor position in OLDADR/OLDCHR regardless of CRSINH
    // (ROM SECS always updates OLDCHR; only the drawing is gated).
    dpoke(s, OLDADR, addr);                  // OLDADR
    poke(s, OLDCHR, atascii_to_internal(c)); // OLDCHR
    // The cursor is only drawn when CRSINH is zero (ROM SECS: if the
    // cursor inhibit flag is set, skip drawing).
    if (peek(s, CRSINH) != 0)
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
    unsigned addr   = dpeek(s, SAVMSC) + row * 40 + col;
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
// LOGCOL — Logical column position within the current logical line (ROM $0063)
// Range: 0..119 (up to 3 physical rows × 40 columns)
// ESCFLG — Escape flag (ROM $02A2)
// When bit 7 is set ($80), the next character is printed verbatim
// TABMAP — Tab stop bitmap (ROM $02A3, 15 bytes = 120 bits)
// Initialized to every 8 columns

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

void atari_editor_init(sim65 s)
{
    poke(s, LMARGN, 2);
    poke(s, RMARGN, 39);
    poke(s, ROWCRS, 0);
    dpoke(s, COLCRS, 2);
    poke(s, DSPFLG, 0);
    poke(s, CRSINH, 0);
    editr_last_row = 0;
    editr_last_col = dpeek(s, COLCRS);
    editr_scr_clear(s);
}

int atari_editor_handle(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
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
