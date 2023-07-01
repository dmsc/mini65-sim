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
#include "atcio.h"
#include "atsio.h"
#include "mathpack.h"
#include "hw.h"
#include <math.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

// Standard put/get character
static int sys_get_char(void)
{
    int c   = getchar();
    if (c == '\n')
        c = 0x9B;
    return c;
}

static int sys_peek_char(void)
{
    int c   = getchar();
    ungetc(c, stdin);
    if (c == '\n')
        c = 0x9B;
    return c;
}

static void sys_put_char(int c)
{
    if (c == 0x9b)
        putchar('\n');
    else if (c == 0x12)
        putchar('-');
    else
        putchar(c);
}

static void sys_put_char_flush(int c)
{
    sys_put_char(c);
    fflush(stdout);
}


// Utility functions
void add_rts_callback(sim65 s, unsigned addr, unsigned len, sim65_callback cb)
{
    unsigned char rts = 0x60;
    sim65_add_callback_range(s, addr, len, cb, sim65_cb_exec);
    for (; len > 0; addr++, len--)
        sim65_add_data_rom(s, addr, &rts, 1);
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

static unsigned dpeek(sim65 s, unsigned addr)
{
    return sim65_get_byte(s, addr) + (sim65_get_byte(s, addr + 1) << 8);
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

static int sim_CH(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    static int read = 0;
    static int ch = 0xFF;
    static uint8_t kcodes[128] = {
// ;    A    B    C    D    E    F    G    H    I    J    K    L    M    N    O
0xA0,0xBF,0x95,0x92,0xBA,0xAA,0xB8,0xBD,0xB9,0x8d,0x81,0x85,0x80,0xA5,0xA3,0x88,
// P    Q    R    S    T    U    V    W    X    Y    Z  ESC    ^    v   <-   ->    _
0x8A,0xAF,0xA8,0xBE,0xAD,0x8B,0x90,0xAE,0x96,0xAB,0x97,0x1C,0x8E,0x8F,0x86,0x87,
//      !    "    #    $    %    &    '    (    )    *    +    ,    -    .    /
0x21,0x5F,0x5E,0x5A,0x58,0x5D,0x5B,0x73,0x70,0x72,0x07,0x06,0x20,0x0E,0x22,0x26,
// 0    1    2    3    4    5    6    7    8    9    :    ;    <    =    >    ?
0x32,0x1F,0x1E,0x1A,0x18,0x1D,0x1B,0x33,0x35,0x30,0x42,0x02,0x36,0x0F,0x37,0x66,
// @    A    B    C    D    E    F    G    H    I    J    K    L    M    N    O
0x75,0x3F,0x15,0x12,0x3A,0x2A,0x38,0x3D,0x39,0x0D,0x01,0x05,0x00,0x25,0x23,0x08,
// P    Q    R    S    T    U    V    W    X    Y    Z    [    \    ]    ^    _
0x0A,0x2F,0x28,0x3E,0x2D,0x0B,0x10,0x2E,0x16,0x2B,0x17,0x60,0x46,0x64,0x47,0x4E,
// `    a    b    c    d    e    f    g    h    i    j    k    l    m    n    o
0xA2,0x7F,0x55,0x52,0x7A,0x6A,0x78,0x7D,0x79,0x4D,0x41,0x45,0x40,0x65,0x63,0x48,
// p    q    r    s    t    u    v    w    x    y    z  C-;    |  CLS   BS  TAB
0x4A,0x6F,0x68,0x7E,0x6D,0x4B,0x50,0x6E,0x56,0x6B,0x57,0x82,0x4F,0x76,0x34,0x2C
    };

    if (data == sim65_cb_read)
    {
        // Return value if we have one
        if (ch != 0xFF)
            return ch;
        // Else, see if we have a character available
        int c = atari_peek_char();
        if (c == EOF)
            return 0xFF;
        else
        {
            // Mark as read
            read = 1;
            // Translate to key-code
            if (c == 0x9B)
                ch = 0x0C;
            else
                ch = kcodes[c & 0x7F];
        }
        return ch;
    }
    else
    {
        // Simply write over our internal value
        ch = data;
        // If we are clearing last character, consume a character read before
        if (ch == 0xFF && read)
        {
            atari_get_char();
            read = 0;
        }
    }
    return 0;
}
// Define our memory map, usable for applications 46.25k.
#define MAX_RAM  (0xD000)       // RAM up to 0xD000 (52k)
#define APP_RAM  (0xC000)       // Usable to applications 0xC000 (48k)
#define LOW_RAM  (0x0700)       // Reserved to the OS up to 0x0700 (1.75k)
#define VID_RAM  (0xC000)       // Video RAM from 0xC000) (4k reserved for video)

static int sim_overwrite_dosvec(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    sim65_dprintf(s, "writing OS ZP memory $%04x", addr);
    return 0;
}

static int sim_os_exit(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    return sim65_err_call_ret;
}

static void atari_bios_init(sim65 s)
{
    // Adds 32 bytes of zeroed RAM at $80
    sim65_add_zeroed_ram(s, 0x80, 0x20);
    // Adds a ROM at 0xE000, to support reads to ROM start
    sim65_add_data_rom(s, 0xE000, (unsigned char*)"\x60", 1);
    // Math Package
    fp_init(s);
    // Simulate keyboard character "CH"
    sim65_add_callback_range(s, 0x2FC, 1, sim_CH, sim65_cb_read);
    sim65_add_callback_range(s, 0x2FC, 1, sim_CH, sim65_cb_write);
    // Simulate RTCLOK
    sim65_add_callback_range(s, 0x12, 3, sim_RTCLOK, sim65_cb_read);
    sim65_add_callback_range(s, 0x12, 3, sim_RTCLOK, sim65_cb_write);
    // Add callbacks to some of OS vectors
    add_rts_callback(s, 0xE471, 1, sim_os_exit);    // BLKBDV
    add_rts_callback(s, 0xE474, 1, sim_os_exit);    // WARMSV
    add_rts_callback(s, 0xE477, 1, sim_os_exit);    // COLDSV

    // Random OS addresses
    poke(s, 8, 0); // WARM START
    poke(s, 17, 0x80); // BREAK key not pressed
    dpoke(s, 10, 0xFFFF); // DOSVEC, go to DOS vector, use $FFFF as simulation return
    sim65_add_callback_range(s, 0x10, 2, sim_overwrite_dosvec, sim65_cb_write);
    dpoke(s, 14, 0x800); // APPHI, lowest usable RAM area
    dpoke(s, 0x2e5, APP_RAM); // MEMTOP
    dpoke(s, 0x2e7, LOW_RAM); // MEMLO
    poke(s, 0x2FC, 0xFF); // CH
    poke(s, 0x2F2, 0xFF); // CH1
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


    { "DDEVIC",   0x0300 }, // PERIPHERAL UNIT 1 BUS I.D. NUMBER
    { "DUNIT ",   0x0301 }, // UNIT NUMBER
    { "DCOMND",   0x0302 }, // BUS COMMAND
    { "DSTATS",   0x0303 }, // COMMAND TYPE/STATUS RETURN
    { "DBUFLO",   0x0304 }, // 1-byte low data buffer address
    { "DBUFHI",   0x0305 }, // 1-byte high data buffer address
    { "DTIMLO",   0x0306 }, // DEVICE TIME OUT IN 1 SECOND UNITS
    { "DUNUSE",   0x0307 }, // UNUSED BYTE
    { "DBYTLO",   0x0308 }, // 1-byte low number of bytes to transfer
    { "DBYTHI",   0x0309 }, // 1-byte high number of bytes to transfer
    { "DAUX1",    0x030A }, // 1-byte first command auxiliary
    { "DAUX2",    0x030B }, // 1-byte second command auxiliary

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

void atari_init(sim65 s, int load_labels, int (*get_char)(void),
                void (*put_char)(int), int emu_dos)
{
    // Init callbacks
    if (get_char)
        atari_get_char = get_char;
    else
        atari_get_char = sys_get_char;
    if (put_char)
        atari_put_char = put_char;
    else
    {
        if(isatty(fileno(stdout)))
            atari_put_char = sys_put_char_flush;
        else
            atari_put_char = sys_put_char;
    }
    atari_peek_char = sys_peek_char;

    // Add 52k of uninitialized ram, maximum possible for the Atari architecture.
    sim65_add_ram(s, 0, MAX_RAM);
    // Add hardware handlers
    atari_hardware_init(s);
    // Add ROM handlers
    atari_bios_init(s);
    atari_cio_init(s, emu_dos);
    atari_sio_init(s);
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
                    return sim65_err_user;
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

enum sim65_error atari_rom_load(sim65 s, int addr, const char *name)
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
        // Fix MEMTOP and RAMTOP
        dpoke(s, 0x2e5, saddr); // MEMTOP
        poke(s, 0x6A, saddr/256); // RAMTOP

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

enum sim65_error atari_boot_image(sim65 s)
{
    return atari_sio_boot(s);
}

int atari_load_image(sim65 s, const char *file_name)
{
    return atari_sio_load_image(s, file_name);
}
