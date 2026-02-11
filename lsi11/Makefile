# lsi11/Makefile — split machine targets:
#   lsi11   : PDP-11/04-like, no MMU, fixed 56KB RAM + DL11 alias
#   pdp1184 : PDP-11/84-like, MMU enabled, default 4096KB RAM

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS ?= -I. -I../core

BUILD_DIR := .build

BIN_LSI11 := lsi11
BIN_PDP1184 := pdp1184
BIN_PDP1134_ALIAS := pdp1134

CFG_LSI11 := -DENABLE_MMU=0 -DMMU_STUB_REGS_WHEN_DISABLED=0 -DLSI11_TARGET_1104=1
CFG_PDP1184 := -DENABLE_MMU=1 -DLSI11_TARGET_1184=1

COMMON_SRCS := \
  bus.c \
  devio.c \
  irq.c \
  util_term.c \
  dev_dl11.c \
  dev_kw11.c \
  dev_rl11.c \
  dev_rh11.c \
  dev_rk11.c \
  dev_lp11.c \
  dev_sr.c

MAIN_SRCS := \
  main.c \
  adapter_core.c

COMMON_OBJS_LSI11 := $(addprefix $(BUILD_DIR)/lsi11/,$(COMMON_SRCS:.c=.o))
MAIN_OBJS_LSI11 := $(addprefix $(BUILD_DIR)/lsi11/,$(MAIN_SRCS:.c=.o))
CORE_OBJS_LSI11 := $(BUILD_DIR)/lsi11/core_core.o $(BUILD_DIR)/lsi11/core_hardware.o $(BUILD_DIR)/lsi11/core_disas.o

COMMON_OBJS_PDP1184 := $(addprefix $(BUILD_DIR)/pdp1184/,$(COMMON_SRCS:.c=.o))
MAIN_OBJS_PDP1184 := $(addprefix $(BUILD_DIR)/pdp1184/,$(MAIN_SRCS:.c=.o))
CORE_OBJS_PDP1184 := $(BUILD_DIR)/pdp1184/core_core.o $(BUILD_DIR)/pdp1184/core_hardware.o $(BUILD_DIR)/pdp1184/core_disas.o

TEST_CFLAGS := $(CFLAGS) -DLSI11_TESTS=1
TEST_COMMON_OBJS_LSI11 := $(addprefix $(BUILD_DIR)/test-lsi11/,$(COMMON_SRCS:.c=.o))
TEST_COMMON_OBJS_PDP1184 := $(addprefix $(BUILD_DIR)/test-pdp1184/,$(COMMON_SRCS:.c=.o))

TESTS_LSI11 := \
  tests/test_bus_map \
  tests/test_irq_semantics \
  tests/test_rl11 \
  tests/test_rk11_regs \
  tests/test_rh11_lsi11_absent

TESTS_PDP1184 := \
  tests/test_rh11_pdp1184

ROOT_MMU_CFLAGS := -Wall -g -I. $(shell sdl2-config --cflags) -DENABLE_MMU=1

all: $(BIN_LSI11) $(BIN_PDP1184) $(BIN_PDP1134_ALIAS)

$(BIN_LSI11): $(MAIN_OBJS_LSI11) $(COMMON_OBJS_LSI11) $(CORE_OBJS_LSI11)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN_PDP1184): $(MAIN_OBJS_PDP1184) $(COMMON_OBJS_PDP1184) $(CORE_OBJS_PDP1184)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN_PDP1134_ALIAS): $(BIN_PDP1184)
	cp -f $(BIN_PDP1184) $(BIN_PDP1134_ALIAS)

$(BUILD_DIR)/lsi11/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CFG_LSI11) -c -o $@ $<

$(BUILD_DIR)/pdp1184/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CFG_PDP1184) -c -o $@ $<

$(BUILD_DIR)/lsi11/core_%.o: ../core/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CFG_LSI11) -c -o $@ $<

$(BUILD_DIR)/pdp1184/core_%.o: ../core/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CFG_PDP1184) -c -o $@ $<

$(BUILD_DIR)/test-lsi11/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) $(CFG_LSI11) -c -o $@ $<

$(BUILD_DIR)/test-lsi11/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) $(CFG_LSI11) -c -o $@ $<

$(BUILD_DIR)/test-pdp1184/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) $(CFG_PDP1184) -c -o $@ $<

$(BUILD_DIR)/test-pdp1184/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) $(CFG_PDP1184) -c -o $@ $<

tests/test_bus_map: $(BUILD_DIR)/test-lsi11/tests/test_bus_map.o $(TEST_COMMON_OBJS_LSI11)
	$(CC) $(CFLAGS) -o $@ $^

tests/test_irq_semantics: $(BUILD_DIR)/test-lsi11/tests/test_irq_semantics.o $(TEST_COMMON_OBJS_LSI11)
	$(CC) $(CFLAGS) -o $@ $^

tests/test_rl11: $(BUILD_DIR)/test-lsi11/tests/test_rl11.o $(TEST_COMMON_OBJS_LSI11)
	$(CC) $(CFLAGS) -o $@ $^

tests/test_rk11_regs: $(BUILD_DIR)/test-lsi11/tests/test_rk11_regs.o $(TEST_COMMON_OBJS_LSI11)
	$(CC) $(CFLAGS) -o $@ $^

tests/test_rh11_lsi11_absent: $(BUILD_DIR)/test-lsi11/tests/test_rh11_lsi11_absent.o $(TEST_COMMON_OBJS_LSI11)
	$(CC) $(CFLAGS) -o $@ $^

tests/test_rh11_pdp1184: $(BUILD_DIR)/test-pdp1184/tests/test_rh11_pdp1184.o $(TEST_COMMON_OBJS_PDP1184)
	$(CC) $(CFLAGS) -o $@ $^

test: test-lsi11

test-lsi11: $(BIN_LSI11) $(TESTS_LSI11)
	@echo "Running bus map test..."
	./tests/test_bus_map
	@echo "Running IRQ semantics test..."
	./tests/test_irq_semantics
	@echo "Running RL11 register/transfer test..."
	./tests/test_rl11
	@echo "Running RK11 register/transfer test..."
	./tests/test_rk11_regs
	@echo "Running RH11 decode test (lsi11 profile)..."
	./tests/test_rh11_lsi11_absent
	@echo "Running lsi11 profile config test..."
	./tests/test_lsi11_config.sh ./$(BIN_LSI11)
	@echo "LSI11 TESTS PASSED"

test-pdp1184: $(BIN_PDP1184) $(TESTS_PDP1184)
	@echo "Running RH11 pdp1184 tests..."
	./tests/test_rh11_pdp1184
	@echo "Running pdp1184 RAM/config tests..."
	./tests/test_pdp1184_ram.sh ./$(BIN_PDP1184)
	@echo "Running core MMU suite (ENABLE_MMU=1)..."
	$(MAKE) -C .. clean
	$(MAKE) -C .. CFLAGS="$(ROOT_MMU_CFLAGS)" test-mmu-on
	@echo "PDP1184 TESTS PASSED"

test-pdp1134: test-pdp1184

test-matrix: test-lsi11 test-pdp1184
	@echo "ALL TESTS PASSED"

clean:
	rm -rf $(BUILD_DIR) $(BIN_LSI11) $(BIN_PDP1184) $(BIN_PDP1134_ALIAS) $(TESTS_LSI11) $(TESTS_PDP1184)

.PHONY: all test test-lsi11 test-pdp1184 test-pdp1134 test-matrix clean
