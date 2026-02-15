CC ?= cc
CFLAGS ?= -Wall -Wpedantic -g

# Choose GUI backend at build time (default SDL2)
GUI ?= sdl

ifeq ($(GUI),x11)
    CFLAGS += -DUSE_X11
    # X11 backend uses X11 library only when needed.
    LDFLAGS += -lX11
    X11_OBJS := x11_gui.o
else
    CFLAGS += -DUSE_SDL
    SDL_CFLAGS := $(shell sdl2-config --cflags)
    SDL_LIBS := $(shell sdl2-config --libs)
    CFLAGS += $(SDL_CFLAGS)
    LDFLAGS += $(SDL_LIBS)
    X11_OBJS :=
endif

CORE_DIR := ../core
# Core emulation source files
# Core object files are located in the parent core directory.
CORE_OBJS := $(CORE_DIR)/core.o $(CORE_DIR)/disas.o $(CORE_DIR)/hardware.o
# Object files for the emulator. X11 GUI objects are added only when the X11
# backend is selected.
OBJS := main.o bk_hw.o bk_tape.o bk_timing.o $(CORE_OBJS) $(X11_OBJS)

all: bk0010-01

bk0010-01: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -I.. -c -o $@ $<

# Compile X11 GUI source only when needed.
ifeq ($(GUI),x11)
x11_gui.o: x11_gui.c x11_gui.h
	$(CC) $(CFLAGS) -I.. -c -o $@ $<
endif

# Compile core sources located in the parent directory
$(CORE_DIR)/%.o: $(CORE_DIR)/%.c
	$(CC) $(CFLAGS) -I.. -c -o $@ $<

clean:
	rm -f $(OBJS) bk0010-01
