# lsi11/Makefile — PDP-11/03-like emulator (plain make, with tests)

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS ?= -I. -I../core

BIN := lsi11

#
# ---------- Common (device/bus/irq) ----------
# These are used by both the main binary and the tests.
#
COMMON_SRCS := \
  bus.c \
  devio.c \
  irq.c \
  util_term.c \
  dev_dl11.c \
  dev_kw11.c \
  dev_rk11.c \
  dev_lp11.c \
  dev_sr.c

COMMON_OBJS := $(COMMON_SRCS:.c=.o)

#
# ---------- Main program ----------
# Depends on core/, adapter_core, and main
#
MAIN_SRCS := \
  main.c \
  adapter_core.c

MAIN_OBJS := $(MAIN_SRCS:.c=.o)

#
# ---------- Tests ----------
#
TEST_CFLAGS := $(CFLAGS) -DLSI11_TESTS=1

TESTS := \
  tests/test_bus_map \
  tests/test_irq_semantics

#
# ---------- Targets ----------
#
all: $(BIN)

CORE_OBJS := ../core/core.o ../core/hardware.o ../core/disas.o

$(BIN): $(MAIN_OBJS) $(COMMON_OBJS) $(CORE_OBJS)
	$(CC) $(CFLAGS) -o $@ $(MAIN_OBJS) $(COMMON_OBJS) $(CORE_OBJS)

#
# ---------- Compile rules ----------
#
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

tests/%.o: tests/%.c
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -c -o $@ $<

#
# ---------- Link tests ----------
# NOTE: tests do NOT link adapter_core.o or main.o
#
TEST_COMMON_OBJS := $(addprefix tests/, $(COMMON_OBJS))

tests/%.o: %.c
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -c -o $@ $<

tests/test_bus_map: tests/test_bus_map.o $(TEST_COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ tests/test_bus_map.o $(TEST_COMMON_OBJS)

tests/test_irq_semantics: tests/test_irq_semantics.o $(TEST_COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ tests/test_irq_semantics.o $(TEST_COMMON_OBJS)

test: $(TESTS)
	@echo "Running bus map test..."
	./tests/test_bus_map
	@echo "Running IRQ semantics test..."
	./tests/test_irq_semantics
	@echo "ALL TESTS PASSED"

#
# ---------- Cleanup ----------
#
clean:
	rm -f $(BIN) *.o tests/*.o $(TESTS)

.PHONY: all test clean
