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
#include <stdlib.h>
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
static uint8_t default_image_data[720 * 128];
static struct {
    uint8_t *data;
    unsigned sec_size;
    unsigned sec_count;
} disk_image = { default_image_data, 128, 720 };

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
        unsigned ssz = hdr[4] | (hdr[5] << 8);
        if (ssz != 128 && ssz != 256)
        {
            sim65_eprintf(s, "%s: unsupported ATR sector size (%d)", file_name, ssz);
            fclose(f);
            return;
        }
        unsigned isz = (hdr[2] << 4) | (hdr[3] << 12) | (hdr[6] << 20);
        // Some images store full size fo the first 3 sectors, others store
        // 128 bytes for those:
        unsigned pad_size = isz % ssz == 0 ? 0 : (ssz - 128) * 3;
        unsigned num_sectors = (isz + pad_size) / ssz;
        if (isz >= 0x1000000 || num_sectors * ssz - pad_size != isz)
        {
            sim65_eprintf(s, "%s: invalid ATR image size (%d)", file_name, isz);
            fclose(f);
            return;
        }
        // Allocate new storage
        uint8_t *data = malloc(ssz * num_sectors);
        // Read 3 first sectors
        for(int i = 0; i < num_sectors; i++)
        {
            if (1 != fread(data + ssz * i, (i < 3 && pad_size) ? 128 : ssz, 1, f))
            {
                sim65_eprintf(s, "%s: ATR file too short at sector %d", file_name, i + 1);
                fclose(f);
                return;
            }
        }
        fclose(f);
        // Ok, copy to image
        if (disk_image.data != default_image_data)
            free(disk_image.data);
        disk_image.data = data;
        disk_image.sec_size = ssz;
        disk_image.sec_count = num_sectors;
        sim65_dprintf(s, "loaded '%s': %d sectors of %d bytes", file_name,
                      num_sectors, ssz);
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
            if(aux < 1 || aux > disk_image.sec_count)
                return SIO_ENAK;
            // First 3 sectors are 128 bytes:
            if(aux < 4 && len != 128)
                return SIO_ENAK;
            if(aux > 3 && len != disk_image.sec_size)
                return SIO_ENAK;

            sim65_dprintf(s, "SIO D%d write sector %d", unit, aux);
            aux --;
            for(int i=0; i<len; i++)
                disk_image.data[aux * disk_image.sec_size + i] = sim65_get_byte(s, addr + i);
            return SIO_OK;

        case 0x52: // Read
            if(0x40 != rw)
                return SIO_ENAK;
            if(aux < 1 || aux > disk_image.sec_count)
                return SIO_ENAK;
            // First 3 sectors are 128 bytes:
            if(aux < 4 && len != 128)
                return SIO_ENAK;
            if(aux > 3 && len != disk_image.sec_size)
                return SIO_ENAK;
            sim65_dprintf(s, "SIO D%d read sector %d", unit, aux);
            aux --;
            sim65_add_data_ram(s, addr, &disk_image.data[aux * disk_image.sec_size], len);
            return SIO_OK;

        case 0x53: // Status request
            if(0x40 != rw)
                return SIO_ENAK;
            if(len != 4)
                return SIO_ENAK;
            sim65_dprintf(s, "SIO D%d status", unit);
            uint8_t status[4] = {
                16,     // command status - drive active
                255,    // hardware status - all bits OK
                224,    // format timeout - standard value
                0       // - unused -
            };
            sim65_add_data_ram(s, addr, status, 4);
            return SIO_OK;

        case 0x21: // Format
            if(0x40 != rw)
                return SIO_ENAK;
            if(len != 128)
                return SIO_ENAK;
            sim65_dprintf(s, "SIO D%d format", unit);
            for(int i=0; i<len; i++)
                poke(s, addr + i, 0xFF);
            return SIO_OK;

        default:
            sim65_dprintf(s, "SIO D%d unknown command $%02x", unit, cmd);
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
    poke(s, 0x0303, e);
    if(e != SIO_OK)
        sim65_set_flags(s, SIM65_FLAG_N, SIM65_FLAG_N);
    else
        sim65_set_flags(s, SIM65_FLAG_N, 0);
    return 0;
}

static int sim_DSKINV(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    int cmd = peek(s, 0x302);
    poke(s, 0x300, 0x31);

    // Read/Write data
    if (cmd == 0x50 || cmd == 0x57) // put or write
        poke(s, 0x303, 0x80);
    else
        poke(s, 0x303, 0x40);

    if (cmd == 0x21)
        poke(s, 0x306, peek(s, 0x246)); // format timeout
    else
        poke(s, 0x306, 0x0F);

    if (cmd == 0x53)    // status
    {
        // Initialize DBUF to DVSTAT (0x2EA), and length to 4
        poke(s, 0x304, 0xEA);
        poke(s, 0x305, 0x02);
        poke(s, 0x308, 0x04);
        poke(s, 0x309, 0x00);
    }
    else
    {
        // Initialize length to sector length
        poke(s, 0x308, peek(s, 0x2D5));
        poke(s, 0x309, peek(s, 0x2D6));
    }
    return sim_SIOV(s, regs, addr, data);
}

static int sim_DINITV(sim65 s, struct sim65_reg *regs, unsigned addr, int data)
{
    poke(s, 0x246, 0xA0);       // format timeout
    poke(s, 0x2D5, 0x80);       // default sector size
    poke(s, 0x2D6, 0x00);
    return 0;
}

void atari_sio_init(sim65 s)
{
    // SIOV
    add_rts_callback(s, SIOV, 1, sim_SIOV);
    // DSKINV - used by MyDOS
    add_rts_callback(s, 0xE453, 1, sim_DSKINV);
    // DINITV
    add_rts_callback(s, 0xE450, 1, sim_DINITV);
    // Init SIO parameters
    sim_DINITV(s, 0, 0, 0);

    // Mark DCB as uninitialized
    sim65_add_ram(s, DDEVIC, 11);

    // Initialize simulating reading of the first sector
    poke(s, 0x300, 0x31);       // Disk
    poke(s, 0x301, 0x01);       // #1
    poke(s, 0x302, 0x52);       // Read
    poke(s, 0x303, 0x40);       // input
    poke(s, 0x304, 0x00);       // buffer = 0x400
    poke(s, 0x305, 0x04);
    poke(s, 0x306, 0x0F);       // timeout = 15
    poke(s, 0x308, 0x80);       // size = 128
    poke(s, 0x309, 0x00);

}

