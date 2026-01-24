CC ?= cc
CFLAGS ?= -Wall -g

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

CORE_DIR := ../core

CORE_OBJS := core_core.o core_disas.o

OBJS := main.o bk_hw.o bk_tape.o bk_timing.o $(CORE_OBJS)

all: bk0010-01

bk0010-01: $(OBJS)
	$(CC) -o $@ $^ $(SDL_LIBS)

main.o: main.c bk_hw.h bk_timing.h $(CORE_DIR)/core.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -I.. -c -o $@ $<

bk_hw.o: bk_hw.c bk_hw.h $(CORE_DIR)/core.h
	$(CC) $(CFLAGS) -I.. -c -o $@ $<

bk_tape.o: bk_tape.c bk_tape.h
	$(CC) $(CFLAGS) -I.. -c -o $@ $<

bk_timing.o: bk_timing.c bk_timing.h $(CORE_DIR)/core.h
	$(CC) $(CFLAGS) -I.. -c -o $@ $<

core_core.o: $(CORE_DIR)/core.c $(CORE_DIR)/core.h
	$(CC) $(CFLAGS) -I.. -c -o $@ $<

core_disas.o: $(CORE_DIR)/disas.c $(CORE_DIR)/disas.h $(CORE_DIR)/core.h
	$(CC) $(CFLAGS) -I.. -c -o $@ $<

clean:
	rm -f $(OBJS) bk0010-01
