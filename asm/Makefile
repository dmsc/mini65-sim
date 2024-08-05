AS=mads

all: ../src/mathpack_bin.h ../src/mathpack_atari.h

../src/mathpack_bin.h: mathpack.bin template.h
	cp -f template.h $@
	xxd -i $< | sed -e 's/^  */    /' >> $@

../src/mathpack_atari.h: mathpack_atari.bin template_atari.h
	cp -f template_atari.h $@
	xxd -i $< | sed -e 's/^  */    /' >> $@

mathpack.bin: compile-mathpack.s mathpack.s
	$(AS) $< -o:$@

mathpack_atari.bin: bb1r2-mathpack.s
	$(AS) $< -o:$@
