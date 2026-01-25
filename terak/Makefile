# Makefile for TERAK-like PDP-11 emulator
# Builds a static library from core/ and the TERAK sources.

CC ?= cc
CFLAGS += -std=c99 -Wall -Wextra -I../core -I./src
AR ?= ar
ARFLAGS = rcs

SRC_DIR := src
CORE_DIR := ../core
BUILD_DIR := build
LIB_DIR := $(BUILD_DIR)/lib
BIN_DIR := $(BUILD_DIR)/bin

CORE_SRCS := $(wildcard $(CORE_DIR)/*.c)
CORE_OBJS := $(patsubst $(CORE_DIR)/%.c,$(BUILD_DIR)/%.o,$(CORE_SRCS))

TERAK_SRCS := $(wildcard $(SRC_DIR)/*.c)
TERAK_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(TERAK_SRCS))

LIBCORE := $(LIB_DIR)/libcore.a
TARGET := $(BIN_DIR)/terak

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR) $(LIB_DIR) $(BIN_DIR)

$(LIBCORE): $(CORE_OBJS) | $(BUILD_DIR)
	$(AR) $(ARFLAGS) $@ $^

$(BUILD_DIR)/%.o: $(CORE_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(LIBCORE) $(TERAK_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TERAK_OBJS) -L$(LIB_DIR) -lcore -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
