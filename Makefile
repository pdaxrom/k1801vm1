all: emu11 dis11 mk90

BASE_CFLAGS = -Wall -g -I. $(shell sdl2-config --cflags)
CFLAGS ?= $(BASE_CFLAGS)

OBJS = core/core.o core/disas.o core/hardware.o

TESTS = tests/core_tests
MMU_TESTS_OFF = tests/test_mmu_disable_build
MMU_TESTS_ON = tests/test_mmu_basic tests/test_mmu_faults tests/test_mmu_splitid tests/test_mmu_22bit
DIAG = tests/cpu_diag

emu11: $(OBJS) emu11.o
	$(CC) -g -o $@ $^ -lncurses

core/core.o: core/core.c core/core.h core/pdp11_fp.c
core/disas.o: core/disas.c core/core.h
core/hardware.o: core/hardware.c core/hardware.h core/core.h

dis11: core/core.o core/disas.o core/hardware.o dis11.o
	$(CC) -o $@ $^

mk90: $(OBJS) mk90.o main.o
	$(CC) -o $@ $^ $(shell sdl2-config --libs) $(VIDEOLIB)

tests/core_tests: tests/core_tests.o core/core.o core/disas.o core/hardware.o
	$(CC) -g -o $@ $^

tests/test_mmu_basic: tests/test_mmu_basic.o core/core.o core/hardware.o
	$(CC) -g -o $@ $^

tests/test_mmu_faults: tests/test_mmu_faults.o core/core.o core/hardware.o
	$(CC) -g -o $@ $^

tests/test_mmu_splitid: tests/test_mmu_splitid.o core/core.o core/hardware.o
	$(CC) -g -o $@ $^

tests/test_mmu_disable_build: tests/test_mmu_disable_build.o core/core.o core/hardware.o
	$(CC) -g -o $@ $^

tests/test_mmu_22bit: tests/test_mmu_22bit.o core/core.o core/hardware.o
	$(CC) -g -o $@ $^

tests/test_mmu_basic.o: tests/test_mmu_basic.c tests/mmu_test_common.h core/core.h core/hardware.h
tests/test_mmu_faults.o: tests/test_mmu_faults.c tests/mmu_test_common.h core/core.h core/hardware.h
tests/test_mmu_splitid.o: tests/test_mmu_splitid.c tests/mmu_test_common.h core/core.h core/hardware.h
tests/test_mmu_disable_build.o: tests/test_mmu_disable_build.c tests/mmu_test_common.h core/core.h core/hardware.h
tests/test_mmu_22bit.o: tests/test_mmu_22bit.c tests/mmu_test_common.h core/core.h core/hardware.h

tests/cpu_diag: tests/cpu_diag_main.o tests/cpu_diag.o core/core.o core/hardware.o
	$(CC) -g -o $@ $^

test:
	$(MAKE) clean
	$(MAKE) CFLAGS='$(BASE_CFLAGS) -DENABLE_MMU=0' $(TESTS) $(MMU_TESTS_OFF)
	./tests/core_tests
	./tests/test_mmu_disable_build

test-mmu-on:
	$(MAKE) clean
	$(MAKE) CFLAGS='$(BASE_CFLAGS) -DENABLE_MMU=1' $(TESTS) $(MMU_TESTS_ON)
	./tests/core_tests
	./tests/test_mmu_basic
	./tests/test_mmu_faults
	./tests/test_mmu_splitid
	./tests/test_mmu_22bit

test-matrix:
	$(MAKE) test
	$(MAKE) test-mmu-on

diag: $(DIAG)
	./tests/cpu_diag --all

clean:
	rm -f $(OBJS) *.o dis11 emu11 mk90 $(TESTS) $(MMU_TESTS_OFF) $(MMU_TESTS_ON) $(DIAG) tests/*.o
