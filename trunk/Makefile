all: emu11 dis11

CFLAGS = -O3 -Wall

OBJS = core/core.o core/disas.o

emu11: $(OBJS) emu11.o
	$(CC) -o $@ $^

dis11: $(OBJS) dis11.o
	$(CC) -o $@ $^

clean:
	rm -f $(OBJS) *.o dis11 emu11
