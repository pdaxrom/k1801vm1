.PHONY: all clean smoke

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -I. -I..
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)
BUILD_DIR := .build
BIN := $(BUILD_DIR)/mk90
CFG := -DENABLE_MMU=0

SRCS := \
	main.c \
	mk90_machine.c \
	mk90_lcd.c \
	mk90_rtc.c \
	mk90_io.c \
	mk90_syscon.c \
	mk90_smp.c \
	mk90_keyboard.c

OBJS := $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))
CORE_OBJS := \
	$(BUILD_DIR)/core_core.o \
	$(BUILD_DIR)/core_disas.o

all: $(BIN)

smoke: $(BIN)
	sh smoke.sh

$(BIN): $(OBJS) $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(SDL_LIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(SDL_CFLAGS) $(CFLAGS) $(CFG) -c -o $@ $<

$(BUILD_DIR)/core_%.o: ../core/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(SDL_CFLAGS) $(CFLAGS) $(CFG) -c -o $@ $<

$(BUILD_DIR)/core_core.o: ../core/core.c ../core/core.h ../core/pdp11_fp.c

clean:
	rm -rf $(BUILD_DIR)
