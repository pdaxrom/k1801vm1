.PHONY: all clean smoke

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -I. -I..
DEPFLAGS := -MMD -MP
BUILD_DIR := .build
BIN := $(BUILD_DIR)/mk90
CFG := -DENABLE_MMU=0

# Choose GUI backend at build time (default SDL2).
# Use:  make GUI=x11   to build with the X11 backend.
GUI ?= sdl

ifeq ($(GUI),x11)
    CPPFLAGS += -DUSE_X11
    SDL_CFLAGS :=
    SDL_LIBS := -lX11
    GUI_SRCS := x11_gui.c
else
    CPPFLAGS += -DUSE_SDL
    SDL_CFLAGS := $(shell sdl2-config --cflags)
    SDL_LIBS := $(shell sdl2-config --libs)
    GUI_SRCS :=
endif

SRCS := \
	main.c \
	mk90_machine.c \
	mk90_lcd.c \
	mk90_rtc.c \
	mk90_io.c \
	mk90_syscon.c \
	mk90_smp.c \
	mk90_keyboard.c \
	$(GUI_SRCS)

OBJS := $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))
CORE_OBJS := \
	$(BUILD_DIR)/core_core.o \
	$(BUILD_DIR)/core_disas.o \
	$(BUILD_DIR)/bk_timing.o
DEPS := $(OBJS:.o=.d) $(CORE_OBJS:.o=.d)

all: $(BIN)

smoke: $(BIN)
	sh smoke.sh

$(BIN): $(OBJS) $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(SDL_LIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(SDL_CFLAGS) $(CFLAGS) $(DEPFLAGS) $(CFG) -c -o $@ $<

$(BUILD_DIR)/core_%.o: ../core/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(SDL_CFLAGS) $(CFLAGS) $(DEPFLAGS) $(CFG) -c -o $@ $<

$(BUILD_DIR)/bk_timing.o: ../bk0010-01/bk_timing.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(SDL_CFLAGS) $(CFLAGS) $(DEPFLAGS) $(CFG) -c -o $@ $<

$(BUILD_DIR)/core_core.o: ../core/core.c ../core/core.h ../core/pdp11_fp.c

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
