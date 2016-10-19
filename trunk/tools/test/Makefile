MACRO11 = ../macro11
DUMPOBJ = ../dumpobj

all: hello.bin test.bin test1.bin test2.bin test3.bin test4.bin test5.bin

.SUFFIXES: .mac .obj .bin

.mac.obj:
	$(MACRO11) $< -o $@ -l $@.lst

.obj.bin:
	$(DUMPOBJ) $< $@ raw

clean:
	rm -f *.obj *.bin *.lst
