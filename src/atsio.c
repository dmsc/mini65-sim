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

/* Implements Atari SIO emulation */
#include "atsio.h"
#include "atari.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// SIO defs
#define SIOV   (0xE459) // Vector to SIO
#define DDEVIC (0x0300) // PERIPHERAL UNIT 1 BUS I.D. NUMBER
#define DUNIT  (0x0301) // UNIT NUMBER
#define DCOMND (0x0302) // BUS COMMAND
#define DSTATS (0x0303) // COMMAND TYPE/STATUS RETURN
#define DBUFLO (0x0304) // 1-byte low data buffer address
#define DBUFHI (0x0305) // 1-byte high data buffer address
#define DTIMLO (0x0306) // DEVICE TIME OUT IN 1 SECOND UNITS
#define DUNUSE (0x0307) // UNUSED BYTE
#define DBYTLO (0x0308) // 1-byte low number of bytes to transfer
#define DBYTHI (0x0309) // 1-byte high number of bytes to transfer
#define DAUX1  (0x030A) // 1-byte first command auxiliary
#define DAUX2  (0x030B) // 1-byte second command auxiliary

#define CHK_READ if(!stat & 0x40) return -1
#define CHK_WRITE if(!stat & 0x80) return -1

// Utility functions
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

/***************************
 * Disk drive simulation.  *
 ***************************/

// SIO errors
enum {
    SIO_OK    = 1,
    SIO_ETIME = 0x8A,
    SIO_ENAK  = 0x8B,
    SIO_EFRAM = 0x8C,
    SIO_EOVER = 0x8E,
    SIO_ECHK  = 0x8F,
    SIO_EDONE = 0x90
};

// The contents of the disk image
static uint8_t disk_image[720 * 128];

// Load disk image from file
void atari_sio_load_image(sim65 s, const char *file_name)
{
    FILE *f = fopen(file_name, "rb");
    if (!f)
    {
        sim65_eprintf(s, "can´t open disk image '%s': %s",
                      file_name, strerror(errno));
    }
    else
    {
        // Get header
        uint8_t hdr[16];
        if (1 != fread(hdr, 16, 1, f))
        {
            sim65_eprintf(s, "%s: can´t read ATR header", file_name);
            fclose(f);
            return;
        }
        if (hdr[0] != 0x96 || hdr[1] != 0x02)
        {
            sim65_eprintf(s, "%s: not an ATR image", file_name);
            fclose(f);
            return;
        }
        unsigned isz = hdr[2] | (hdr[3] << 8);
        unsigned ssz = hdr[4] | (hdr[5] << 8);
        if (isz != 0x1680 || ssz != 128 || hdr[6] != 0)
        {
            sim65_eprintf(s, "%s: only 720 SD image supported", file_name);
            fclose(f);
            return;
        }
        if (720 != fread(disk_image, 128, 720, f))
        {
            sim65_eprintf(s, "%s: ATR file too short", file_name);
            fclose(f);
            return;
        }
        fclose(f);
    }
}

// Minimal simulation of a disk drive
static unsigned sio_disk(sim65 s, int unit, int cmd, int stat, int addr, int len, int aux)
{
    if (unit != 1)
        return SIO_ETIME;

    int rw = stat & 0xC0;
    switch(cmd)
    {
        case 0x50: // Write
        case 0x57: // Write with verify
            if(0x80 != rw)
                return SIO_ENAK;
            if(len != 128)
                return SIO_ENAK;
            if(aux < 1 || aux > 720)
                return SIO_ENAK;
            sim65_dprintf(s, "SIO D%d write sector %d", unit, aux);
            for(int i=0; i<len; i++)
                disk_image[aux * 128 - 128 + i] = sim65_get_byte(s, addr + i);
            return SIO_OK;

        case 0x52: // Read
            if(0x40 != rw)
                return SIO_ENAK;
            if(len != 128)
                return SIO_ENAK;
            if(aux < 1 || aux > 720)
                return SIO_ENAK;
            sim65_dprintf(s, "SIO D%d read sector %d", unit, aux);
            sim65_add_data_ram(s, addr, &disk_image[aux * 128 - 128], len);
            return SIO_OK;

        case 0x53: // Status request
            if(0x80 != rw)
                return SIO_ENAK;
            if(len != 4)
                return SIO_ENAK;
            sim65_dprintf(s, "SIO D%d status", unit);
            uint8_t status[4] = {
                0,      // command status
                0,      // hardware status
                1,      // controller timeout
                0       // - unused -
            };
            sim65_add_data_ram(s, addr, status, 4);
            return SIO_OK;

        case 0x21: // Format
            if(0x80 != rw)
                return SIO_ENAK;
            if(len != 128)
                return SIO_ENAK;
            sim65_dprintf(s, "SIO D%d format", unit);
            for(int i=0; i<len; i++)
                poke(s, addr + i, 0);
            return SIO_OK;
    }
    return SIO_ENAK;
}

static int sim_SIOV(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    // Read DCB
    unsigned ddevic = peek(s, 0x0300);
    unsigned dunit  = peek(s, 0x0301);
    unsigned dcomnd = peek(s, 0x0302);
    unsigned dstats = peek(s, 0x0303);
    unsigned dbuf   = dpeek(s, 0x0304);
    unsigned dtimlo = peek(s, 0x0306);
    unsigned dbyt   = dpeek(s, 0x0308);
    unsigned daux1  = peek(s, 0x030A);
    unsigned daux2  = peek(s, 0x030B);

    sim65_dprintf(s, "SIO call"
            " dev=%02x:%02x cmd=%02x stat=%02x buf=%04x tim=%02x len=%04x aux=%02x:%02x",
            ddevic, dunit, dcomnd, dstats, dbuf, dtimlo, dbyt, daux1, daux2);

    int e;
    if(ddevic == 0x31)
        e = sio_disk(s, dunit, dcomnd, dstats, dbuf, dbyt, daux1 + (daux2 << 8));
    else
        e = SIO_ETIME;

    regs->y = e;
    if(e != SIO_OK)
        sim65_set_flags(s, SIM65_FLAG_N, SIM65_FLAG_N);
    else
        sim65_set_flags(s, SIM65_FLAG_N, 0);
    return 0;
}

void atari_sio_init(sim65 s)
{
    // CIOV
    add_rts_callback(s, SIOV, 1, sim_SIOV);
    // Mark DCB as uninitialized
    sim65_add_ram(s, DDEVIC, 11);
}

