all: emu11 dis11 mk90

CFLAGS = -Wall -g -I. $(shell sdl2-config --cflags)

OBJS = core/core.o core/disas.o core/hardware.o

TESTS = tests/core_tests

emu11: $(OBJS) emu11.o
	$(CC) -g -o $@ $^ -lncurses

core/core.o: core/core.c core/core.h
core/disas.o: core/disas.c core/core.h
core/hardware.o: core/hardware.c core/hardware.h

dis11: core/disas.o core/hardware.o dis11.o
	$(CC) -o $@ $^

mk90: $(OBJS) mk90.o main.o
	$(CC) -o $@ $^ $(shell sdl2-config --libs) $(VIDEOLIB)

tests/core_tests: tests/core_tests.o core/core.o core/hardware.o
	$(CC) -g -o $@ $^

test: $(TESTS)
	./tests/core_tests

clean:
	rm -f $(OBJS) *.o dis11 emu11 mk90 $(TESTS) tests/*.o
