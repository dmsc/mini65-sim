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
#include "sim65.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAXRAM (0x10000)

// Memory status codes
#define ms_ram    1
#define ms_rom    2
#define ms_valid  4

// Simulator errors
#define err_exec_undef   1
#define err_exec_uninit  2
#define err_read_undef   3
#define err_read_uninit  4
#define err_write_undef  5
#define err_write_rom    6
#define err_break        7
#define err_invalid_ins  8

// Instruction lengths
static unsigned ilen[256] = {
    1,2,1,1,1,2,2,1,1,2,1,1,1,3,3,1, 2,2,1,1,1,2,2,1,1,3,1,1,1,3,3,1,
    3,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1, 2,2,1,1,1,2,2,1,1,3,1,1,1,3,3,1,
    1,2,1,1,1,2,2,1,1,2,1,1,3,3,3,1, 2,2,1,1,1,2,2,1,1,3,1,1,1,3,3,1,
    1,2,1,1,1,2,2,1,1,2,1,1,3,3,3,1, 2,2,1,1,1,2,2,1,1,3,1,1,1,3,3,1,
    1,2,1,1,2,2,2,1,1,1,1,1,3,3,3,1, 2,2,1,1,2,2,2,1,1,3,1,1,1,3,1,1,
    2,2,2,1,2,2,2,1,1,2,1,1,3,3,3,1, 2,2,1,1,2,2,2,1,1,3,1,1,3,3,3,1,
    2,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1, 2,2,1,1,1,2,2,1,1,3,1,1,1,3,3,1,
    2,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1, 2,2,1,1,1,2,2,1,1,3,1,1,1,3,3,1
};

struct sim65s
{
    unsigned debug;
    unsigned error;
    struct sim65_reg r;
    unsigned char mem[MAXRAM];
    unsigned char mems[MAXRAM];
    sim65_callback vec[MAXRAM];
};

sim65 sim65_new()
{
    sim65 s = calloc(sizeof(struct sim65s), 1);
    s->r.s = 0xFF;
    s->r.p = 0x34;
    return s;
}

void sim65_add_ram(sim65 s, unsigned addr, unsigned len)
{
    unsigned end = addr + len;
    if (addr >= MAXRAM)
        return;
    if (end >= MAXRAM)
        end = MAXRAM;
    for (; addr < end; addr++)
        s->mems[addr] |= ms_ram;
}

void sim65_add_zeroed_ram(sim65 s, unsigned addr, unsigned len)
{
    unsigned end = addr + len;
    if (addr >= MAXRAM)
        return;
    if (end >= MAXRAM)
        end = MAXRAM;
    for (; addr < end; addr++)
    {
        s->mems[addr] = ms_ram | ms_valid;
        s->mem[addr] = 0;
    }
}

void sim65_add_data_ram(sim65 s, unsigned addr, const unsigned char *data, unsigned len)
{
    unsigned end = addr + len;
    if (addr >= MAXRAM)
        return;
    if (end >= MAXRAM)
        end = MAXRAM;
    for (; addr < end; addr++, data++)
    {
        s->mems[addr] = ms_ram | ms_valid;
        s->mem[addr] = *data;
    }
}

void sim65_add_data_rom(sim65 s, unsigned addr, const unsigned char *data, unsigned len)
{
    unsigned end = addr + len;
    if (addr >= MAXRAM)
        return;
    if (end >= MAXRAM)
        end = MAXRAM;
    for (; addr < end; addr++, data++)
    {
        s->mems[addr] = ms_rom | ms_valid;
        s->mem[addr] = *data;
    }
}

void sim65_add_callback(sim65 s, unsigned addr, sim65_callback cb)
{
    if (addr >= MAXRAM)
        return;
    s->vec[addr] = cb;
}

void sim65_add_callback_range(sim65 s, unsigned addr, unsigned len, sim65_callback cb)
{
    unsigned i;
    for (i = addr; i < addr + len && i < MAXRAM; i++)
        sim65_add_callback(s, i, cb);
}

unsigned sim65_get_byte(sim65 s, unsigned addr)
{
    if (addr >= MAXRAM)
        return 0x100;
    if (!(s->mems[addr] & ms_valid))
        return 0x100;
    return s->mem[addr];
}

void set_error(sim65 s, unsigned e)
{
    if( !s->error )
        s->error = e;
}

static uint8_t readPc(sim65 s)
{
    s->r.pc &= 0xFFFF;
    if (!(s->mems[s->r.pc] & ms_valid))
    {
        if (!(s->mems[s->r.pc]))
            set_error( s, err_exec_undef );
        else
            set_error( s, err_exec_uninit );
    }
    uint8_t ret = s->mem[s->r.pc];
    s->r.pc = (s->r.pc + 1) & 0xFFFF;
    return ret;
}

static uint8_t readByte(sim65 s, unsigned addr)
{
    addr &= 0xFFFF;
    if (s->vec[addr])
    {
        int e = s->vec[addr](s, &s->r, addr, SIM65_CB_READ);
        if (e < 0)
            set_error( s, e );
        return e;
    }
    if (!(s->mems[addr] & ms_valid))
    {
        if (!(s->mems[addr]))
            set_error( s, err_read_undef );
        else
        {
            fprintf(stderr, "err: reading uninitialized value at $%4X\n", addr);
            // set_error( s, err_read_uninit ); // Common enough!
            s->mems[addr] |= ms_valid;
        }
    }
    return s->mem[addr];
}

static void writeByte(sim65 s, unsigned addr, unsigned val)
{
    addr &= 0xFFFF;
    if (s->vec[addr])
    {
        int e = s->vec[addr](s, &s->r, addr, val);
        if (e)
            set_error( s, e );
    }
    else if (!s->mems[addr])
        set_error( s, err_write_undef );
    else if (s->mems[addr] & ms_rom)
        set_error( s, err_write_rom );
    else
    {
        s->mem[addr] = val;
        s->mems[addr] |= ms_valid;
    }
}

static uint16_t readWord(sim65 s, unsigned addr)
{
    uint16_t d1 = readByte(s, addr);
    return d1 | (readByte(s, addr + 1) << 8);
}

static uint8_t readIndX(sim65 s, unsigned addr)
{
    addr = readWord(s, (addr + s->r.x) & 0xFF);
    return readByte(s, addr);
}

static int readIndY(sim65 s, unsigned addr)
{
    addr = readWord(s, addr & 0xFF);
    return readByte(s, 0xFFFF & (addr + s->r.y));
}

static void writeIndX(sim65 s, unsigned addr, unsigned val)
{
    addr = readWord(s, (addr + s->r.x) & 0xFF);
    writeByte(s, addr, val);
}

static void writeIndY(sim65 s, unsigned addr, unsigned val)
{
    addr = readWord(s, addr & 0xFF);
    writeByte(s, 0xFFFF & (addr + s->r.y), val);
}

#define FLAG_C 0x01
#define FLAG_Z 0x02
#define FLAG_I 0x04
#define FLAG_D 0x08
#define FLAG_B 0x10
#define FLAG_V 0x40
#define FLAG_N 0x80

#define SETZ(a) s->r.p = (s->r.p & ~FLAG_Z) | ((a)&0xFF ? 0 : FLAG_Z)
#define SETC(a) s->r.p = (s->r.p & ~FLAG_C) | ((a) ? FLAG_C : 0)
#define SETV(a) s->r.p = (s->r.p & ~FLAG_V) | ((a) ? FLAG_V : 0)
#define SETD(a) s->r.p = (s->r.p & ~FLAG_D) | ((a) ? FLAG_D : 0)
#define SETN(a) s->r.p = (s->r.p & ~FLAG_N) | ((a)&0x80 ? FLAG_N : 0)
#define SETI(a) s->r.p = (s->r.p & ~FLAG_I) | ((a) ? FLAG_I : 0)
#define GETC    (s->r.p & FLAG_C)
#define GETD    (s->r.p & FLAG_D)

// Implements ADC instruction, adding the accumulator with the given value.
static void do_adc(sim65 s, unsigned val)
{
    if (GETD)
    {
        // Decimal mode:
        // Z flag is computed as the binary version.
        unsigned tmp = s->r.a + val + (GETC ? 1 : 0);
        SETZ(tmp);
        // ADC value is computed in decimal
        tmp = (s->r.a & 0xF) + (val & 0xF) + (GETC ? 1 : 0);
        if (tmp >= 10)
            tmp = (tmp - 10) | 16;
        tmp += (s->r.a & 0xF0) + (val & 0xF0);
        SETN(tmp);
        SETV(!((s->r.a ^ val) & 0x80) && ((val ^ tmp) & 0x80));
        if (tmp > 0x9F)
            tmp += 0x60;
        SETC(tmp > 0xFF);
        s->r.a = tmp & 0xFF;
    }
    else
    {
        // Binary mode
        unsigned tmp = s->r.a + val + (GETC ? 1 : 0);
        SETV(((~(s->r.a ^ val)) & (s->r.a ^ tmp)) & 0x80);
        //  SETV(!((s->r.a ^ val) & 0x80) && ((val ^ tmp) & 0x80));
        SETC(tmp > 0xFF);
        SETN(tmp);
        SETZ(tmp);
        s->r.a = tmp & 0xFF;
    }
}

// Implements SBC instruction, subtract the accumulator to the given value.
static void do_sbc(sim65 s, unsigned val)
{
    if (GETD)
    {
        // Decimal mode:
        val = val ^ 0xFF;

        // Z and V flags computed as the binary version.
        unsigned tmp = s->r.a + val + (GETC ? 1 : 0);
        SETV((((s->r.a ^ val)) & (s->r.a ^ tmp)) & 0x80);
        SETZ(tmp);

        // ADC value is computed in decimal
        tmp = (s->r.a & 0xF) + (val & 0xF) + (GETC ? 1 : 0);
        if (tmp < 0x10)
            tmp = (tmp - 6) & 0x0F;

        tmp += (s->r.a & 0xF0) + (val & 0xF0);
        if (tmp < 0x100)
            tmp = (tmp - 0x60) & 0xFF;

        SETN(tmp);
        SETC(tmp > 0xFF);
        s->r.a = tmp & 0xFF;
    }
    else
    {
        // Binary mode
        unsigned tmp = s->r.a + 0xFF - val + (GETC ? 1 : 0);
        SETV((((s->r.a ^ val)) & (s->r.a ^ tmp)) & 0x80);
        SETC(tmp > 0xFF);
        SETN(tmp);
        SETZ(tmp);
        s->r.a = tmp & 0xFF;
    }
}

#define ACCR   val = s->r.a
#define ACCW   s->r.a = val
#define ZP_R1  val = readByte(s, data & 0xFF)
#define ZP_W1  writeByte(s, data & 0xFF, val)
#define ZPX_R  val = readByte(s, (data + s->r.x) & 0xFF)
#define ZPX_W  writeByte(s, (data + s->r.x) & 0xFF, val)
#define ZPY_R  val = readByte(s, (data + s->r.y) & 0xFF)
#define ZPY_W  writeByte(s, (data + s->r.y) & 0xFF, val)
#define ABS_R  val = readByte(s, data)
#define ABS_W  writeByte(s, data, val)
#define ABX_R  val = readByte(s, data + s->r.x)
#define ABX_W  writeByte(s, data + s->r.x, val)
#define ABY_R  val = readByte(s, data + s->r.y)
#define ABY_W  writeByte(s, data + s->r.y, val)
#define IND_X(op)  val = readIndX(s, data); op
#define IND_Y  val = readIndY(s, data)
#define INDW_X writeIndX(s, data, val)
#define INDW_Y writeIndY(s, data, val)
#define IND16  val = readWord(s, data)

#define ORA s->r.a |= val; SETZ(s->r.a); SETN(s->r.a)
#define AND s->r.a &= val; SETZ(s->r.a); SETN(s->r.a)
#define EOR s->r.a ^= val; SETZ(s->r.a); SETN(s->r.a)
#define ADC do_adc(s, val)
#define SBC do_sbc(s, val)
#define BIT SETN(val); SETV(val & 0x40); SETZ(s->r.a & val)
#define ASL SETC(val & 0x80); val = (val << 1) & 0xFF; SETZ(val); SETN(val)
#define ROL val = (val << 1) | (GETC ? 1 : 0); SETC(val & 256); val &= 0xFF; SETZ(val); SETN(val)
#define LSR SETC(val & 1); val=(val >> 1) & 0xFF; SETZ(val); SETN(val)
#define ROR val |= (GETC ? 256 : 0); SETC(val & 1); val = (val >> 1) & 0xFF; SETZ(val); SETN(val)
#define DEC val = (val - 1) & 0xFF; SETN(val); SETZ(val)
#define INC val = (val + 1) & 0xFF; SETN(val); SETZ(val)
#define CMP val = (s->r.a + 0x100 - val); SETZ(val); SETN(val); SETC(val > 0xFF)
#define CPX val = (s->r.x + 0x100 - val); SETZ(val); SETN(val); SETC(val > 0xFF)
#define CPY val = (s->r.y + 0x100 - val); SETZ(val); SETN(val); SETC(val > 0xFF)

#define LDA SETN(val); SETZ(val); s->r.a = val
#define LDX SETN(val); SETZ(val); s->r.x = val
#define LDY SETN(val); SETZ(val); s->r.y = val
#define STA val = s->r.a
#define STX val = s->r.x
#define STY val = s->r.y
#define BRA s->r.pc = ((data & 0x80) ? s->r.pc + data - 0x100 : s->r.pc + data) & 0xFFFF
#define PUSH writeByte(s, 0x100 + s->r.s,val); s->r.s = (s->r.s - 1) & 0xFF
#define POP  s->r.s = (s->r.s + 1) & 0xFF; val = readByte(s, 0x100 + s->r.s)

// Complete ops
#define ZP_R(op)   ZP_R1; op
#define ZP_W(op)   op; ZP_W1
#define ZP_RW(op)  ZP_R1; op; ZP_W1
#define IMM(op)    val = data; op

#define BRA_0(a) if (!(s->r.p & a)) BRA
#define BRA_1(a) if ((s->r.p & a)) BRA
#define JSR do_jsr(s, data)
#define RTS do_rts(s)
#define RTI do_rti(s)

#define POP_P  POP; s->r.p = val | 0x30

static void do_jsr(sim65 s, unsigned data)
{
    unsigned val;
    s->r.pc = (s->r.pc - 1) & 0xFFFF;
    val = (s->r.pc & 0xFF00) >> 8;
    PUSH;
    val = s->r.pc & 0xFF;
    PUSH;
    s->r.pc = data;
}

static void do_rts(sim65 s)
{
    unsigned val;
    POP;
    s->r.pc = val;
    POP;
    s->r.pc |= val << 8;
    s->r.pc = (s->r.pc + 1) & 0xFFFF;
}

static void do_rti(sim65 s)
{
    unsigned val;
    POP_P;
    POP;
    s->r.pc = val;
    POP;
    s->r.pc |= val << 8;
    s->r.pc = (s->r.pc) & 0xFFFF;
}

static void next(sim65 s)
{
    unsigned ins, data, val;

    // See if out vector
    if (s->vec[s->r.pc])
        s->vec[s->r.pc](s, &s->r, s->r.pc, SIM65_CB_EXEC);

    if (s->debug)
        sim65_print_reg(s);

    ins = readPc(s);
    if (ilen[ins] == 2)
        data = readPc(s);
    else if (ilen[ins] == 3)
    {
        data = readPc(s);
        data |= readPc(s) << 8;
    }
    switch (ins)
    {
        case 0x00:  set_error( s, err_break ); break;
        case 0x01:  IND_X(ORA);             break;
        case 0x05:  ZP_R(ORA);              break;
        case 0x06:  ZP_RW(ASL);             break;
        case 0x08:  val = s->r.p; PUSH;     break; // PHP
        case 0x09:  IMM(ORA);               break;
        case 0x0A:  ACCR;   ASL;   ACCW;    break;
        case 0x0D:  ABS_R;  ORA;            break;
        case 0x0E:  ABS_R;  ASL;   ABS_W;   break;
        case 0x10:  BRA_0(FLAG_N);          break; // BPL
        case 0x11:  IND_Y;  ORA;            break;
        case 0x15:  ZPX_R;  ORA;            break;
        case 0x16:  ZPX_R;  ASL;   ZPX_W;   break;
        case 0x18:  SETC(0);                break; // CLC
        case 0x19:  ABY_R;  ORA;            break;
        case 0x1d:  ABX_R;  ORA;            break;
        case 0x1e:  ABX_R;  ASL;   ABX_W;   break;
        case 0x20:  JSR;                    break; // JSR
        case 0x21:  IND_X(AND);             break;
        case 0x24:  ZP_R(BIT);              break;
        case 0x25:  ZP_R(AND);              break;
        case 0x26:  ZP_RW(ROL);             break;
        case 0x28:  POP_P;                  break; // PLP
        case 0x29:  IMM(AND);               break;
        case 0x2a:  ACCR;   ROL;   ACCW;    break;
        case 0x2c:  ABS_R;  BIT;            break;
        case 0x2d:  ABS_R;  AND;            break;
        case 0x2e:  ABS_R;  ROL;   ABS_W;   break;
        case 0x30:  BRA_1(FLAG_N);          break;
        case 0x31:  IND_Y;  AND;            break;
        case 0x35:  ZPX_R;  AND;            break;
        case 0x36:  ZPX_R;  ROL;   ZPX_W;   break;
        case 0x38:  SETC(1);                break; // SEC
        case 0x39:  ABY_R;  AND;            break;
        case 0x3d:  ABX_R;  AND;            break;
        case 0x3e:  ABX_R;  ROL;   ABX_W;   break;
        case 0x40:  RTI;                    break; // RTI
        case 0x41:  IND_X(EOR);             break;
        case 0x45:  ZP_R(EOR);              break;
        case 0x46:  ZP_RW(LSR);             break;
        case 0x48:  STA;    PUSH;           break; // PHA
        case 0x49:  IMM(EOR);               break;
        case 0x4a:  ACCR;   LSR;   ACCW;    break;
        case 0x4c:  s->r.pc = data;         break; // JMP
        case 0x4d:  ABS_R;  EOR;            break;
        case 0x4e:  ABS_R;  LSR;   ABS_W;   break;
        case 0x50:  BRA_0(FLAG_V);          break;
        case 0x51:  IND_Y;  EOR;            break;
        case 0x55:  ZPX_R;  EOR;            break;
        case 0x56:  ZPX_R;  LSR;   ZPX_W;   break;
        case 0x58:  SETI(0);                break; // CLI
        case 0x59:  ABY_R;  EOR;            break;
        case 0x5d:  ABX_R;  EOR;            break;
        case 0x5e:  ABX_R;  LSR;   ABX_W;   break;
        case 0x60:  RTS;                    break; // RTS
        case 0x61:  IND_X(ADC);             break;
        case 0x65:  ZP_R(ADC);              break;
        case 0x66:  ZP_RW(ROR);             break;
        case 0x68:  POP;    LDA;            break; // PLA
        case 0x69:  IMM(ADC);               break;
        case 0x6a:  ACCR;   ROR;   ACCW;    break;
        case 0x6c:  IND16;  s->r.pc = val;  break; // JMP ()
        case 0x6d:  ABS_R;  ADC;            break;
        case 0x6e:  ABS_R;  ROR;   ABS_W;   break;
        case 0x70:  BRA_1(FLAG_V);          break;
        case 0x71:  IND_Y;  ADC;            break;
        case 0x75:  ZPX_R;  ADC;            break;
        case 0x76:  ZPX_R;  ROR;   ZPX_W;   break;
        case 0x78:  SETI(1);                break; // SEI
        case 0x79:  ABY_R;  ADC;            break;
        case 0x7d:  ABX_R;  ADC;            break;
        case 0x7e:  ABX_R;  ROR;   ABX_W;   break;
        case 0x81:  STA;   INDW_X;          break;
        case 0x84:  ZP_W(STY);              break;
        case 0x85:  ZP_W(STA);              break;
        case 0x86:  ZP_W(STX);              break;
        case 0x88:  STY;   DEC;   LDY;      break; // DEY
        case 0x8a:  STX;   LDA;             break; // TXA
        case 0x8c:  STY;   ABS_W;           break;
        case 0x8d:  STA;   ABS_W;           break;
        case 0x8e:  STX;   ABS_W;           break;
        case 0x90:  BRA_0(FLAG_C);          break; // BCC
        case 0x91:  STA;   INDW_Y;          break;
        case 0x94:  STY;   ZPX_W;           break;
        case 0x95:  STA;   ZPX_W;           break;
        case 0x96:  STX;   ZPY_W;           break;
        case 0x98:  STY;   LDA;             break; // TYA
        case 0x99:  STA;   ABY_W;           break;
        case 0x9a:  s->r.s = s->r.x;        break; // TXS
        case 0x9d:  STA;   ABX_W;           break;
        case 0xa0:  IMM(LDY);               break;
        case 0xa1:  IND_X(LDA);             break;
        case 0xa2:  IMM(LDX);               break;
        case 0xa4:  ZP_R(LDY);              break;
        case 0xa5:  ZP_R(LDA);              break;
        case 0xa6:  ZP_R(LDX);              break;
        case 0xa8:  STA;     LDY;           break; // TAY
        case 0xa9:  IMM(LDA);               break;
        case 0xaa:  STA;     LDX;           break; // TAX
        case 0xac:  ABS_R;   LDY;           break;
        case 0xad:  ABS_R;   LDA;           break;
        case 0xae:  ABS_R;   LDX;           break;
        case 0xb0:  BRA_1(FLAG_C);          break; // BCS
        case 0xb1:  IND_Y;   LDA;           break;
        case 0xb4:  ZPX_R;   LDY;           break;
        case 0xb5:  ZPX_R;   LDA;           break;
        case 0xb6:  ZPY_R;   LDX;           break;
        case 0xb8:  SETV(0);                break; // CLV
        case 0xb9:  ABY_R;   LDA;           break;
        case 0xba:  val = s->r.s;  LDX;     break; // TSX
        case 0xbc:  ABX_R;   LDY;           break;
        case 0xbd:  ABX_R;   LDA;           break;
        case 0xbe:  ABY_R;   LDX;           break;
        case 0xc0:  IMM(CPY);               break;
        case 0xc1:  IND_X(CMP);             break;
        case 0xc4:  ZP_R(CPY);              break;
        case 0xc5:  ZP_R(CMP);              break;
        case 0xc6:  ZP_RW(DEC);             break;
        case 0xc8:  STY;    INC;  LDY;      break; // INY
        case 0xc9:  IMM(CMP);               break;
        case 0xca:  STX;   DEC;   LDX;      break; // DEX
        case 0xcc:  ABS_R;  CPY;            break;
        case 0xcd:  ABS_R;  CMP;            break;
        case 0xce:  ABS_R;  DEC;  ABS_W;    break;
        case 0xd0:  BRA_0(FLAG_Z);          break; // BNE
        case 0xd1:  IND_Y;   CMP;           break;
        case 0xd5:  ZPX_R;   CMP;           break;
        case 0xd6:  ZPX_R;   DEC;  ZPX_W;   break;
        case 0xd8:  SETD(0);                break; // CLD
        case 0xd9:  ABY_R;   CMP;           break;
        case 0xdd:  ABX_R;   CMP;           break;
        case 0xde:  ABX_R;   DEC;  ABX_W;   break;
        case 0xe0:  IMM(CPX);               break;
        case 0xe1:  IND_X(SBC);             break;
        case 0xe4:  ZP_R(CPX);              break;
        case 0xe5:  ZP_R(SBC);              break;
        case 0xe6:  ZP_RW(INC);             break;
        case 0xe8:  STX;    INC;  LDX;      break; // INX
        case 0xe9:  IMM(SBC);               break;
        case 0xea:  ;                       break; // NOP
        case 0xec:  ABS_R;  CPX;            break;
        case 0xed:  ABS_R;  SBC;            break;
        case 0xee:  ABS_R;  INC;  ABS_W;    break;
        case 0xf0:  BRA_1(FLAG_Z);          break; // BEQ
        case 0xf1:  IND_Y;   SBC;           break;
        case 0xf5:  ZPX_R;   SBC;           break;
        case 0xf6:  ZPX_R;   INC;  ZPX_W;   break;
        case 0xf8:  SETD(1);                break; // SED
        case 0xf9:  ABY_R;   SBC;           break;
        case 0xfd:  ABX_R;   SBC;           break;
        case 0xfe:  ABX_R;   INC;  ABX_W;   break;
        default:    set_error( s, err_invalid_ins );
    }
}

int sim65_run(sim65 s, struct sim65_reg *regs, unsigned addr)
{
    if (regs)
        memcpy(&s->r, regs, sizeof(*regs));

    s->error = 0;
    s->r.pc = addr;
    while (!s->error)
        next(s);

    if (regs)
        memcpy(regs, &s->r, sizeof(*regs));

    return s->error;
}

static void print_mem(char *buf, sim65 s, unsigned addr)
{
    addr &= 0xFFFF;
    if (s->mems[addr] & ms_valid)
    {
        if (s->mems[addr] & ms_ram)
            sprintf(buf, "[%02X]", s->mem[addr]);
        else
            sprintf(buf, "{%02X}", s->mem[addr]);
    }
    else if (s->mems[addr])
        memcpy(buf, "[UU]", 4);
    else
        memcpy(buf, "[NN]", 4);
}

void sim65_set_debug(sim65 s, unsigned level)
{
    s->debug = level;
}

static void print_mem_count(char *buf, sim65 s, unsigned addr, unsigned len)
{
    unsigned i;
    for (i = 0; i < len; i++)
        print_mem(buf + i * 4, s, addr + i);
}

#define PNAME(name, str...)  memcpy(buf, name, 3); c = sprintf(buf+4, str)

#define INSPRT_IMM(name) PNAME(name, "#$%02x",            data);
#define INSPRT_BRA(name) PNAME(name, "$%04x",             s->r.pc + 2 + (signed char)data);
#define INSPRT_ABS(name) PNAME(name, "$%04x",             data);
#define INSPRT_ABX(name) PNAME(name, "$%04x,X",           data);
#define INSPRT_ABY(name) PNAME(name, "$%04x,Y",           data);
#define INSPRT_ZPG(name) PNAME(name, "$%02x",             data);
#define INSPRT_ZPX(name) PNAME(name, "$%02x,X",           data);
#define INSPRT_ZPY(name) PNAME(name, "$%02x,Y",           data);
#define INSPRT_IDX(name) PNAME(name, "($%02x,X) [$%04x]", data, readWord(s, data + s->r.x));
#define INSPRT_IDY(name) PNAME(name, "($%02x),Y [$%04x]", data, readWord(s, data) + s->r.y);
#define INSPRT_IND(name) PNAME(name, "($%04x)",           data);
#define INSPRT_IMP(name) PNAME(name, "");
#define INSPRT_ACC(name) PNAME(name, "A");

static void print_curr_ins(char *buf, sim65 s)
{
    unsigned ins = 0, data = 0, c = 0;
    ins = s->mem[(s->r.pc) & 0xFFFF];
    if (ilen[ins] == 2)
        data = s->mem[(1 + s->r.pc) & 0xFFFF];
    else if (ilen[ins] == 3)
        data = s->mem[(1 + s->r.pc) & 0xFFFF] + (s->mem[(2 + s->r.pc) & 0xFFFF] << 8);
    memset(buf, ' ', 30);
    switch (ins)
    {
        case 0x00: INSPRT_IMP("BRK"); break;
        case 0x01: INSPRT_IDX("ORA"); break;
        case 0x02: INSPRT_IMP("kil"); break;
        case 0x03: INSPRT_IDY("slo"); break;
        case 0x04: INSPRT_ZPG("dop"); break;
        case 0x05: INSPRT_ZPG("ORA"); break;
        case 0x06: INSPRT_ZPG("ASL"); break;
        case 0x07: INSPRT_ZPG("slo"); break;
        case 0x08: INSPRT_IMP("PHP"); break;
        case 0x09: INSPRT_IMM("ORA"); break;
        case 0x0A: INSPRT_ACC("ASL"); break;
        case 0x0B: INSPRT_IMM("aac"); break;
        case 0x0C: INSPRT_ABS("top"); break;
        case 0x0D: INSPRT_ABS("ORA"); break;
        case 0x0E: INSPRT_ABS("ASL"); break;
        case 0x0F: INSPRT_ABS("slo"); break;
        case 0x10: INSPRT_BRA("BPL"); break;
        case 0x11: INSPRT_IDY("ORA"); break;
        case 0x12: INSPRT_IMP("kil"); break;
        case 0x13: INSPRT_IDX("slo"); break;
        case 0x14: INSPRT_ZPX("dop"); break;
        case 0x15: INSPRT_ZPX("ORA"); break;
        case 0x16: INSPRT_ZPX("ASL"); break;
        case 0x17: INSPRT_ZPX("slo"); break;
        case 0x18: INSPRT_IMP("CLC"); break;
        case 0x19: INSPRT_ABY("ORA"); break;
        case 0x1A: INSPRT_IMP("nop"); break;
        case 0x1B: INSPRT_ABY("slo"); break;
        case 0x1C: INSPRT_ABX("top"); break;
        case 0x1D: INSPRT_ABX("ORA"); break;
        case 0x1E: INSPRT_ABX("ASL"); break;
        case 0x1F: INSPRT_ABX("slo"); break;
        case 0x20: INSPRT_ABS("JSR"); break;
        case 0x21: INSPRT_IDX("AND"); break;
        case 0x22: INSPRT_IMP("kil"); break;
        case 0x23: INSPRT_IDX("rla"); break;
        case 0x24: INSPRT_ZPG("BIT"); break;
        case 0x25: INSPRT_ZPG("AND"); break;
        case 0x26: INSPRT_ZPG("ROL"); break;
        case 0x27: INSPRT_ZPG("rla"); break;
        case 0x28: INSPRT_IMP("PLP"); break;
        case 0x29: INSPRT_IMM("AND"); break;
        case 0x2A: INSPRT_ACC("ROL"); break;
        case 0x2B: INSPRT_IMM("aac"); break;
        case 0x2C: INSPRT_ABS("BIT"); break;
        case 0x2D: INSPRT_ABS("AND"); break;
        case 0x2E: INSPRT_ABS("ROL"); break;
        case 0x2F: INSPRT_ABS("rla"); break;
        case 0x30: INSPRT_BRA("BMI"); break;
        case 0x31: INSPRT_IDY("AND"); break;
        case 0x32: INSPRT_IMP("kil"); break;
        case 0x33: INSPRT_IDY("rla"); break;
        case 0x34: INSPRT_ZPX("dop"); break;
        case 0x35: INSPRT_ZPX("AND"); break;
        case 0x36: INSPRT_ZPX("ROL"); break;
        case 0x37: INSPRT_ZPX("rla"); break;
        case 0x38: INSPRT_IMP("SEC"); break;
        case 0x39: INSPRT_ABY("AND"); break;
        case 0x3A: INSPRT_IMP("nop"); break;
        case 0x3B: INSPRT_ABY("rla"); break;
        case 0x3C: INSPRT_ABX("top"); break;
        case 0x3D: INSPRT_ABX("AND"); break;
        case 0x3E: INSPRT_ABX("ROL"); break;
        case 0x3F: INSPRT_ABX("rla"); break;
        case 0x40: INSPRT_IMP("RTI"); break;
        case 0x41: INSPRT_IDX("EOR"); break;
        case 0x42: INSPRT_IMP("kil"); break;
        case 0x43: INSPRT_IDX("sre"); break;
        case 0x44: INSPRT_ZPG("dop"); break;
        case 0x45: INSPRT_ZPG("EOR"); break;
        case 0x46: INSPRT_ZPG("LSR"); break;
        case 0x47: INSPRT_ZPG("sre"); break;
        case 0x48: INSPRT_IMP("PHA"); break;
        case 0x49: INSPRT_IMM("EOR"); break;
        case 0x4A: INSPRT_ACC("LSR"); break;
        case 0x4B: INSPRT_IMM("asr"); break;
        case 0x4C: INSPRT_ABS("JMP"); break;
        case 0x4D: INSPRT_ABS("EOR"); break;
        case 0x4E: INSPRT_ABS("LSR"); break;
        case 0x4F: INSPRT_ABS("sre"); break;
        case 0x50: INSPRT_BRA("BVC"); break;
        case 0x51: INSPRT_IDY("EOR"); break;
        case 0x52: INSPRT_IMP("kil"); break;
        case 0x53: INSPRT_IDY("sre"); break;
        case 0x54: INSPRT_ZPX("dop"); break;
        case 0x55: INSPRT_ZPX("EOR"); break;
        case 0x56: INSPRT_ZPX("LSR"); break;
        case 0x57: INSPRT_ZPX("sre"); break;
        case 0x58: INSPRT_IMP("CLI"); break;
        case 0x59: INSPRT_ABY("EOR"); break;
        case 0x5A: INSPRT_IMP("nop"); break;
        case 0x5B: INSPRT_ABY("sre"); break;
        case 0x5C: INSPRT_ABX("top"); break;
        case 0x5D: INSPRT_ABX("EOR"); break;
        case 0x5E: INSPRT_ABX("LSR"); break;
        case 0x5F: INSPRT_ABX("sre"); break;
        case 0x60: INSPRT_IMP("RTS"); break;
        case 0x61: INSPRT_IDX("ADC"); break;
        case 0x62: INSPRT_IMP("kil"); break;
        case 0x63: INSPRT_IDX("rra"); break;
        case 0x64: INSPRT_ZPG("dop"); break;
        case 0x65: INSPRT_ZPG("ADC"); break;
        case 0x66: INSPRT_ZPG("ROR"); break;
        case 0x67: INSPRT_ZPG("rra"); break;
        case 0x68: INSPRT_IMP("PLA"); break;
        case 0x69: INSPRT_IMM("ADC"); break;
        case 0x6A: INSPRT_ACC("ROR"); break;
        case 0x6B: INSPRT_IMM("arr"); break;
        case 0x6C: INSPRT_IND("JMP"); break;
        case 0x6D: INSPRT_ABS("ADC"); break;
        case 0x6E: INSPRT_ABS("ROR"); break;
        case 0x6F: INSPRT_ABS("rra"); break;
        case 0x70: INSPRT_BRA("BVS"); break;
        case 0x71: INSPRT_IDY("ADC"); break;
        case 0x72: INSPRT_IMP("kil"); break;
        case 0x73: INSPRT_IDY("rra"); break;
        case 0x74: INSPRT_ZPX("dop"); break;
        case 0x75: INSPRT_ZPX("ADC"); break;
        case 0x76: INSPRT_ZPX("ROR"); break;
        case 0x77: INSPRT_ZPX("rra"); break;
        case 0x78: INSPRT_IMP("SEI"); break;
        case 0x79: INSPRT_ABY("ADC"); break;
        case 0x7A: INSPRT_IMP("nop"); break;
        case 0x7B: INSPRT_ABY("rra"); break;
        case 0x7C: INSPRT_ABX("top"); break;
        case 0x7D: INSPRT_ABX("ADC"); break;
        case 0x7E: INSPRT_ABX("ROR"); break;
        case 0x7F: INSPRT_ABX("rra"); break;
        case 0x80: INSPRT_IMM("dop"); break;
        case 0x81: INSPRT_IDX("STA"); break;
        case 0x82: INSPRT_IMM("dop"); break;
        case 0x83: INSPRT_IDX("aax"); break;
        case 0x84: INSPRT_ZPG("STY"); break;
        case 0x85: INSPRT_ZPG("STA"); break;
        case 0x86: INSPRT_ZPG("STX"); break;
        case 0x87: INSPRT_ZPG("aax"); break;
        case 0x88: INSPRT_IMP("DEY"); break;
        case 0x89: INSPRT_IMM("dop"); break;
        case 0x8A: INSPRT_IMP("TXA"); break;
        case 0x8B: INSPRT_IMM("xaa"); break;
        case 0x8C: INSPRT_ABS("STY"); break;
        case 0x8D: INSPRT_ABS("STA"); break;
        case 0x8E: INSPRT_ABS("STX"); break;
        case 0x8F: INSPRT_ABS("aax"); break;
        case 0x90: INSPRT_BRA("BCC"); break;
        case 0x91: INSPRT_IDY("STA"); break;
        case 0x92: INSPRT_IMP("kil"); break;
        case 0x93: INSPRT_IDY("axa"); break;
        case 0x94: INSPRT_ZPX("STY"); break;
        case 0x95: INSPRT_ZPX("STA"); break;
        case 0x96: INSPRT_ZPY("STX"); break;
        case 0x97: INSPRT_ZPX("aax"); break;
        case 0x98: INSPRT_IMP("TYA"); break;
        case 0x99: INSPRT_ABY("STA"); break;
        case 0x9A: INSPRT_IMP("TXS"); break;
        case 0x9B: INSPRT_ABY("xas"); break;
        case 0x9C: INSPRT_ABX("sya"); break;
        case 0x9D: INSPRT_ABX("STA"); break;
        case 0x9E: INSPRT_ABY("sxa"); break;
        case 0x9F: INSPRT_ABY("axa"); break;
        case 0xA0: INSPRT_IMM("LDY"); break;
        case 0xA1: INSPRT_IDX("LDA"); break;
        case 0xA2: INSPRT_IMM("LDX"); break;
        case 0xA3: INSPRT_IDX("lax"); break;
        case 0xA4: INSPRT_ZPG("LDY"); break;
        case 0xA5: INSPRT_ZPG("LDA"); break;
        case 0xA6: INSPRT_ZPG("LDX"); break;
        case 0xA7: INSPRT_ZPG("lax"); break;
        case 0xA8: INSPRT_IMP("TAY"); break;
        case 0xA9: INSPRT_IMM("LDA"); break;
        case 0xAA: INSPRT_IMP("TAX"); break;
        case 0xAB: INSPRT_IMP("atx"); break;
        case 0xAC: INSPRT_ABS("LDY"); break;
        case 0xAD: INSPRT_ABS("LDA"); break;
        case 0xAE: INSPRT_ABS("LDX"); break;
        case 0xAF: INSPRT_ABS("lax"); break;
        case 0xB0: INSPRT_BRA("BCS"); break;
        case 0xB1: INSPRT_IDY("LDA"); break;
        case 0xB2: INSPRT_IMP("kil"); break;
        case 0xB3: INSPRT_IDY("lax"); break;
        case 0xB4: INSPRT_ZPX("LDY"); break;
        case 0xB5: INSPRT_ZPX("LDA"); break;
        case 0xB6: INSPRT_ZPY("LDX"); break;
        case 0xB7: INSPRT_ZPX("lax"); break;
        case 0xB8: INSPRT_IMP("CLV"); break;
        case 0xB9: INSPRT_ABY("LDA"); break;
        case 0xBA: INSPRT_IMP("TSX"); break;
        case 0xBB: INSPRT_ABY("lar"); break;
        case 0xBC: INSPRT_ABX("LDY"); break;
        case 0xBD: INSPRT_ABX("LDA"); break;
        case 0xBE: INSPRT_ABY("LDX"); break;
        case 0xBF: INSPRT_ABY("lax"); break;
        case 0xC0: INSPRT_IMM("CPY"); break;
        case 0xC1: INSPRT_IDX("CMP"); break;
        case 0xC2: INSPRT_IMM("dop"); break;
        case 0xC3: INSPRT_IDX("dcp"); break;
        case 0xC4: INSPRT_ZPG("CPY"); break;
        case 0xC5: INSPRT_ZPG("CMP"); break;
        case 0xC6: INSPRT_ZPG("DEC"); break;
        case 0xC7: INSPRT_ZPG("dcp"); break;
        case 0xC8: INSPRT_IMP("INY"); break;
        case 0xC9: INSPRT_IMM("CMP"); break;
        case 0xCA: INSPRT_IMP("DEX"); break;
        case 0xCB: INSPRT_IMM("axs"); break;
        case 0xCC: INSPRT_ABS("CPY"); break;
        case 0xCD: INSPRT_ABS("CMP"); break;
        case 0xCE: INSPRT_ABS("DEC"); break;
        case 0xCF: INSPRT_ABS("dcp"); break;
        case 0xD0: INSPRT_BRA("BNE"); break;
        case 0xD1: INSPRT_IDY("CMP"); break;
        case 0xD2: INSPRT_IMP("kil"); break;
        case 0xD3: INSPRT_IDY("dcp"); break;
        case 0xD4: INSPRT_ZPX("dop"); break;
        case 0xD5: INSPRT_ZPX("CMP"); break;
        case 0xD6: INSPRT_ZPX("DEC"); break;
        case 0xD7: INSPRT_ZPX("dcp"); break;
        case 0xD8: INSPRT_IMP("CLD"); break;
        case 0xD9: INSPRT_ABY("CMP"); break;
        case 0xDA: INSPRT_IMP("nop"); break;
        case 0xDB: INSPRT_ABY("dcp"); break;
        case 0xDC: INSPRT_ABX("top"); break;
        case 0xDD: INSPRT_ABX("CMP"); break;
        case 0xDE: INSPRT_ABX("DEC"); break;
        case 0xDF: INSPRT_ABX("dcp"); break;
        case 0xE0: INSPRT_IMM("CPX"); break;
        case 0xE1: INSPRT_IDX("SBC"); break;
        case 0xE2: INSPRT_IMM("dop"); break;
        case 0xE3: INSPRT_IDX("isc"); break;
        case 0xE4: INSPRT_ZPG("CPX"); break;
        case 0xE5: INSPRT_ZPG("SBC"); break;
        case 0xE6: INSPRT_ZPG("INC"); break;
        case 0xE7: INSPRT_ZPG("isc"); break;
        case 0xE8: INSPRT_IMP("INX"); break;
        case 0xE9: INSPRT_IMM("SBC"); break;
        case 0xEA: INSPRT_IMP("NOP"); break;
        case 0xEB: INSPRT_IMM("sbc"); break;
        case 0xEC: INSPRT_ABS("CPX"); break;
        case 0xED: INSPRT_ABS("SBC"); break;
        case 0xEE: INSPRT_ABS("INC"); break;
        case 0xEF: INSPRT_ABS("isc"); break;
        case 0xF0: INSPRT_BRA("BEQ"); break;
        case 0xF1: INSPRT_IDY("SBC"); break;
        case 0xF2: INSPRT_IMP("kil"); break;
        case 0xF3: INSPRT_IDY("isc"); break;
        case 0xF4: INSPRT_ZPX("dop"); break;
        case 0xF5: INSPRT_ZPX("SBC"); break;
        case 0xF6: INSPRT_ZPX("INC"); break;
        case 0xF7: INSPRT_ZPX("isc"); break;
        case 0xF8: INSPRT_IMP("SED"); break;
        case 0xF9: INSPRT_ABY("SBC"); break;
        case 0xFA: INSPRT_IMP("nop"); break;
        case 0xFB: INSPRT_ABY("isc"); break;
        case 0xFC: INSPRT_ABX("top"); break;
        case 0xFD: INSPRT_ABX("SBC"); break;
        case 0xFE: INSPRT_ABX("INC"); break;
        case 0xFF: INSPRT_ABX("isc"); break;
    }
    buf[4 + c] = ' ';
    buf[21] = ';';
    c = ilen[ins];
    print_mem_count(buf + 23, s, s->r.pc, c);
    buf[23 + c * 4] = '\n';
    buf[24 + c * 4] = 0;
}

void sim65_print_reg(sim65 s)
{
    char buf[128];
    sprintf(buf, "A=%02X X=%02X Y=%02X P=%02X S=%02X PC=%04X : ", // 35 chars
            s->r.a, s->r.x, s->r.y, s->r.p, s->r.s, s->r.pc);
    print_curr_ins(buf + 35, s);
    fputs(buf, stderr);
}
