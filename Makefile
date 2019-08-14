#
#  Mini65 - Small 6502 simulator with Atari 8bit bios.
#  Copyright (C) 2017-2019 Daniel Serpell
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program.  If not, see <http://www.gnu.org/licenses/>
#

CC=gcc
INCLUDES=-Iccan
CFLAGS=$(INCLUDES) -O3 -Wall -g -flto
LDLIBS=-lm

BDIR=build
ODIR=$(BDIR)/obj

all: $(BDIR)/atarisim

SRC=\
 src/atari.c\
 src/dosfname.c\
 src/hw.c\
 src/main.c\
 src/mathpack.c\
 src/sim65.c\

OBJS=$(SRC:src/%.c=$(ODIR)/%.o)

$(BDIR)/atarisim: $(OBJS) | $(BDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(ODIR)/%.o: src/%.c | $(ODIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BDIR):
$(ODIR):
	mkdir -p $@


$(ODIR)/atari.o: src/atari.c src/atari.h src/sim65.h src/mathpack.h src/dosfname.h
$(ODIR)/dosfname.o: src/dosfname.c src/dosfname.h
$(ODIR)/hw.o: src/hw.c src/hw.h src/sim65.h
$(ODIR)/main.o: src/main.c src/atari.h src/sim65.h src/hw.h
$(ODIR)/mathpack.o: src/mathpack.c src/mathpack.h src/sim65.h src/mathpack_bin.h
$(ODIR)/sim65.o: src/sim65.c src/sim65.h
