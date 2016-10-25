all: emu11 dis11

CFLAGS = -O2 -Wall -g

OBJS = core/core.o core/disas.o

emu11: $(OBJS) emu11.o
	$(CC) -g -o $@ $^ -lncurses

dis11: $(OBJS) dis11.o
	$(CC) -o $@ $^

clean:
	rm -f $(OBJS) *.o dis11 emu11
