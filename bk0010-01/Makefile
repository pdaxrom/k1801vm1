CC ?= cc
CFLAGS ?= -Wall -g

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

CORE_DIR := ../core

OBJS := main.o bk_hw.o $(CORE_DIR)/core.o $(CORE_DIR)/disas.o

all: bk0010-01

bk0010-01: $(OBJS)
	$(CC) -o $@ $^ $(SDL_LIBS)

main.o: main.c bk_hw.h $(CORE_DIR)/core.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -I.. -c -o $@ $<

bk_hw.o: bk_hw.c bk_hw.h $(CORE_DIR)/core.h
	$(CC) $(CFLAGS) -I.. -c -o $@ $<

$(CORE_DIR)/core.o: $(CORE_DIR)/core.c $(CORE_DIR)/core.h
	$(CC) $(CFLAGS) -I.. -c -o $@ $<

$(CORE_DIR)/disas.o: $(CORE_DIR)/disas.c $(CORE_DIR)/disas.h $(CORE_DIR)/core.h
	$(CC) $(CFLAGS) -I.. -c -o $@ $<

clean:
	rm -f $(OBJS) bk0010-01
