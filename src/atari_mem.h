#ifndef ATARI_MEM_H
#define ATARI_MEM_H

#include "sim65.h"

/* Byte and word accessors for the emulated 6502 address space. */
static inline void poke(sim65 s, unsigned addr, unsigned char val)
{
    sim65_add_data_ram(s, addr, &val, 1);
}

static inline void dpoke(sim65 s, unsigned addr, unsigned val)
{
    poke(s, addr, val & 0xFF);
    poke(s, addr + 1, val >> 8);
}

/* Reads are 8-bit: invalid/uninitialized memory (0x100) maps to zero. */
static inline unsigned peek(sim65 s, unsigned addr)
{
    return sim65_get_byte(s, addr) & 0xFF;
}

static inline unsigned dpeek(sim65 s, unsigned addr)
{
    return peek(s, addr) + (peek(s, addr + 1) << 8);
}

#endif
