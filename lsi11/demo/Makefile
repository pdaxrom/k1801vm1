MASM = ../../microasm11/microasm11

all: hello.bin dl11_irq.bin periph_test.bin dl11_test.bin kw11_test.bin lp11_test.bin rk11_test.bin rk11_wr_test.bin sr_test.bin rk11_boot.bin

.SUFFIXES: .asm .bin

.asm.bin:
	$(MASM) -binary --list $@.lst $< $@

clean:
	rm -f *.bin *.lst

.PHONY: all clean
