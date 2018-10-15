AS=mads

all: ../src/mathpack_bin.h

../src/mathpack_bin.h: mathpack.bin template.h
	cp -f template.h $@
	xxd -i $< >> $@

mathpack.bin: compile-mathpack.s mathpack.s
	$(AS) $< -o:$@

