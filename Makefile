CC=gcc
INCLUDES=-Iccan
CFLAGS=$(INCLUDES) -O3 -Wall -g -flto
LDLIBS=-lm

BDIR=build
ODIR=$(BDIR)/obj

all: $(BDIR)/atarisim

SRC=\
 src/abios.c\
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


$(ODIR)/abios.o: src/abios.c src/abios.h src/sim65.h src/mathpack.h
$(ODIR)/hw.o: src/hw.c src/hw.h src/sim65.h
$(ODIR)/main.o: src/main.c src/abios.h src/sim65.h src/hw.h
$(ODIR)/mathpack.o: src/mathpack.c src/mathpack.h src/sim65.h src/mathpack_bin.h
$(ODIR)/sim65.o: src/sim65.c src/sim65.h
