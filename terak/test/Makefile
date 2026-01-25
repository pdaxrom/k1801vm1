# Build the hello test program using the macro11 assembler.

#MACRO11 = ../../MACRO11/src/macro11
#DUMPOBJ = ../../MACRO11/src/dumpobj
MACRO11 = ../../tools/macro11
DUMPOBJ = ../../tools/dumpobj

all: hello.bin

.SUFFIXES: .mac .obj .bin

.mac.obj:
	$(MACRO11) $< -o $@ -l $@.lst

.obj.bin:
	$(DUMPOBJ) $< $@ raw

clean:
	rm -f *.obj *.bin *.lst

.PHONY: all clean
