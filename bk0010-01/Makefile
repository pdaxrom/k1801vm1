CC ?= cc
CFLAGS ?= -Wall -Wpedantic -g
CPPFLAGS ?= -I..
DEPFLAGS := -MMD -MP
CORE_CONFIG := -DENABLE_MMU=0 -DMMU_STUB_REGS_WHEN_DISABLED=0
.DEFAULT_GOAL := all

# Choose GUI backend at build time (default SDL2)
GUI ?= sdl
BUILD_DIR := .build/$(GUI)

ifeq ($(GUI),x11)
    CFLAGS += -DUSE_X11
    # X11 backend uses X11 library only when needed.
    LDFLAGS += -lX11
    X11_OBJS := $(BUILD_DIR)/x11_gui.o
else
    CFLAGS += -DUSE_SDL
    SDL_CFLAGS := $(shell sdl2-config --cflags)
    SDL_LIBS := $(shell sdl2-config --libs)
    CFLAGS += $(SDL_CFLAGS)
    LDFLAGS += $(SDL_LIBS)
    X11_OBJS :=
endif

CORE_DIR := ../core
CORE_OBJS := $(BUILD_DIR)/core/core.o $(BUILD_DIR)/core/disas.o \
	$(BUILD_DIR)/core/hardware.o
# Object files for the emulator. X11 GUI objects are added only when the X11
# backend is selected.
OBJS := $(BUILD_DIR)/main.o $(BUILD_DIR)/bk_hw.o $(BUILD_DIR)/bk_tape.o \
	$(BUILD_DIR)/bk_timing.o $(CORE_OBJS) $(X11_OBJS)
DEPS := $(OBJS:.o=.d)

-include $(DEPS)

all: bk0010-01

bk0010-01: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $(CORE_CONFIG) -c -o $@ $<

# Compile X11 GUI source only when needed.
ifeq ($(GUI),x11)
$(BUILD_DIR)/x11_gui.o: x11_gui.c x11_gui.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $(CORE_CONFIG) -c -o $@ $<
endif

# Compile private core objects so other targets cannot leave an incompatible
# ENABLE_MMU variant behind in ../core.
$(BUILD_DIR)/core/%.o: $(CORE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $(CORE_CONFIG) -c -o $@ $<

clean:
	rm -rf .build bk0010-01
