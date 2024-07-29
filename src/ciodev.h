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

/* Tables for implementing CIO devices */
#pragma once

#include "sim65.h"

// Helper macros
#define LO(a) ((a)&0xFF)
#define HI(a) ((a) >> 8)

// HATAB defs
#define EDITRV (0xE400)
#define SCRENV (0xE410)
#define KEYBDV (0xE420)
#define PRINTV (0xE430)
#define CASETV (0xE440)
#define DISKDV (0xE3F0) // Emulated DOS, does not exists in real hardware!

#define EDITOR_OFFSET (6) // NOTE: Editor must be entry at offset "6"

#define HATABS_ENTRY(a, b) (a), LO(b), HI(b)

// Offsets in handler table:  function
#define DEVR_OPEN (0) //      3
#define DEVR_CLOSE (1) //     12
#define DEVR_GET (2) //       4,5,6,7
#define DEVR_PUT (3) //       8.9,10,11
#define DEVR_STATUS (4) //    13
#define DEVR_SPECIAL (5) //   14 and up
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

// Fake function handler addresses, 8 bytes for each device
#define EDITR_BASE 0xE500
#define SCREN_BASE 0xE508
#define KEYBD_BASE 0xE510
#define PRINT_BASE 0xE518
#define CASET_BASE 0xE520
#define DISKD_BASE 0xE528

// Address for DOS handlers
#define DOSCP_BASE  0xE540
#define COMTAB_BASE 0xE550

// IOCB in zero page
#define ICHIDZ (0x20) //        HANDLER INDEX NUMBER (FF = IOCB FREE)
#define ICDNOZ (0x21) //        DEVICE NUMBER (DRIVE NUMBER)
#define ICCOMZ (0x22) //        COMMAND CODE
#define ICSTAZ (0x23) //        STATUS OF LAST IOCB ACTION
#define ICBALZ (0x24) //        BUFFER ADDRESS LOW BYTE
#define ICBAHZ (0x25) //        1-byte high buffer address
#define ICPTLZ (0x26) //        PUT BYTE ROUTINE ADDRESS -1
#define ICPTHZ (0x27) //        1-byte high PUT-BYTE routine address
#define ICBLLZ (0x28) //        BUFFER LENGTH LOW BYTE
#define ICBLHZ (0x29) //        1-byte high buffer length
#define ICAX1Z (0x2A) //        AUXILIARY INFORMATION FIRST BYTE
#define ICAX2Z (0x2B) //        1-byte second auxiliary information
#define ICSPRZ (0x2C) //        HANDLE address
#define ICIDNO (0x2E) //        IOCB NUMBER X 16
#define CIOCHR (0x2F) //        CHARACTER BYTE FOR CURRENT OPERATION

// Main IOCB
#define IOCB (0x0340) //        I/O control block
#define ICHID (0x0340) //       HANDLER INDEX NUMBER (FF=IOCB FREE)
#define ICDNO (0x0341) //       DEVICE NUMBER (DRIVE NUMBER)
#define ICCOM (0x0342) //       COMMAND CODE
#define ICSTA (0x0343) //       STATUS OF LAST IOCB ACTION
#define ICBAL (0x0344) //       1-byte low buffer address
#define ICBAH (0x0345) //       1-byte high buffer address
#define ICPTL (0x0346) //       1-byte low PUT-BYTE routine address - 1
#define ICPTH (0x0347) //       1-byte high PUT-BYTE routine address - 1
#define ICBLL (0x0348) //       1-byte low buffer length
#define ICBLH (0x0349) //       1-byte high buffer length
#define ICAX1 (0x034A) //       1-byte first auxiliary information
#define ICAX2 (0x034B) //       1-byte second auxiliary information
#define ICAX3 (0x034C) //       1-byte third auxiliary information
#define ICAX4 (0x034D) //       1-byte fourth auxiliary information
#define ICAX5 (0x034E) //       1-byte fifth auxiliary information
#define ICSPR (0x034F) //       SPARE BYTE
