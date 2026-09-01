PYTHON ?= python3
IVERILOG ?= iverilog
VVP ?= vvp
VERILATOR ?= verilator
VERILATOR_JOBS ?= 4
ASM11 ?= ../microasm11/microasm11
RT11_IMAGE ?= $(CURDIR)/images/rt11v503.dsk

DIAMOND_HOME ?= $(HOME)/.local/lscc/diamond/3.14
DIAMOND_LIBSTDCPP ?= /lib/x86_64-linux-gnu/libstdc++.so.6
DIAMONDC ?= $(DIAMOND_HOME)/bin/lin64/diamondc
PROGRAMMER_HOME ?= $(HOME)/.local/lscc/programmer/diamond/3.14
PGRCMD ?= $(PROGRAMMER_HOME)/bin/lin64/pgrcmd

BOARD := boards/hc1200-microcomp
UCODE := ucode/experimental/am4
BUILD := build
MICROM_BUILD := $(BUILD)/mc.rom
BOOT_BIN := $(BOARD)/am4_sd_boot.bin
SERVICE_BIN := $(BOARD)/am4_rk_service.bin
BOOT_ROM := $(BOARD)/am4_sd_boot.rom
MICROM_EBR := $(BOARD)/am4_mcrom_ebr.v
JED := $(BOARD)/impl1-am4/microcomp-am4_impl1.jed
XCF := $(BUILD)/hc1200-am4.xcf

.PHONY: all firmware microcode-check test test-fast test-rt11 \
	test-rt11-ctrlc test-rt11-ctrlc-full test-vendor project-test host-test \
	diamond xcf program clean distclean help
.DELETE_ON_ERROR:

all: $(MICROM_EBR)

$(BUILD):
	mkdir -p $@

$(MICROM_BUILD): $(UCODE)/mc.asm $(UCODE)/assemble_mcrom.py \
		$(UCODE)/tools/meta29.py $(UCODE)/tools/am29_m4.def | $(BUILD)
	$(PYTHON) $(UCODE)/assemble_mcrom.py $(UCODE)/mc.asm $@ \
		--listing $(BUILD)/mc.lst
	@cmp -s $@ $(UCODE)/mc.rom || { \
		echo "ERROR: assembled MicROM differs from checked-in $(UCODE)/mc.rom"; \
		exit 1; \
	}

microcode-check: $(MICROM_BUILD)
	$(PYTHON) scripts/microcode_diff.py \
		$(UCODE)/upstream/mc-original.rom $(MICROM_BUILD)

$(BOOT_BIN): $(UCODE)/sd_boot.asm
	@test -x "$(ASM11)" || { echo "microasm11 not found: $(ASM11)"; exit 1; }
	$(ASM11) -binary --cpu dcj-11 --list $(BOARD)/am4_sd_boot.lst \
		$< $@ > $(BOARD)/am4_sd_boot.asm.log 2>&1 || \
		{ cat $(BOARD)/am4_sd_boot.asm.log; exit 1; }

$(SERVICE_BIN): $(UCODE)/rk_service.asm
	@test -x "$(ASM11)" || { echo "microasm11 not found: $(ASM11)"; exit 1; }
	$(ASM11) -binary --cpu dcj-11 --list $(BOARD)/am4_rk_service.lst \
		$< $@ > $(BOARD)/am4_rk_service.asm.log 2>&1 || \
		{ cat $(BOARD)/am4_rk_service.asm.log; exit 1; }

$(MICROM_EBR): $(MICROM_BUILD) $(BOOT_BIN) $(SERVICE_BIN) \
		$(UCODE)/make_mcrom_ebr.py
	$(PYTHON) $(UCODE)/make_mcrom_ebr.py $(MICROM_BUILD) \
		--boot-bin $(BOOT_BIN) --service-bin $(SERVICE_BIN) \
		--boot-rom-output $(BOOT_ROM) > $@

firmware: $(MICROM_EBR)

host-test:
	$(MAKE) -C host/hg test

test-fast: all host-test
	$(MAKE) -C testbench test-fast-local \
		ASM11="$(abspath $(ASM11))" PYTHON="$(PYTHON)" \
		IVERILOG="$(IVERILOG)" VVP="$(VVP)"

test: all
	$(MAKE) -C testbench test-local \
		ASM11="$(abspath $(ASM11))" PYTHON="$(PYTHON)" \
		IVERILOG="$(IVERILOG)" VVP="$(VVP)" \
		VERILATOR="$(VERILATOR)" VERILATOR_JOBS="$(VERILATOR_JOBS)"

test-rt11: all
	$(MAKE) -C testbench am4-rt11-boot-fast \
		ASM11="$(abspath $(ASM11))" PYTHON="$(PYTHON)" \
		VERILATOR="$(VERILATOR)" VERILATOR_JOBS="$(VERILATOR_JOBS)" \
		RT11_IMAGE="$(abspath $(RT11_IMAGE))"

test-rt11-ctrlc: all
	$(MAKE) -C testbench am4-rt11-ctrlc \
		ASM11="$(abspath $(ASM11))" PYTHON="$(PYTHON)" \
		VERILATOR="$(VERILATOR)" VERILATOR_JOBS="$(VERILATOR_JOBS)" \
		RT11_IMAGE="$(abspath $(RT11_IMAGE))"

test-rt11-ctrlc-full: all
	$(MAKE) -C testbench am4-rt11-ctrlc-full \
		ASM11="$(abspath $(ASM11))" PYTHON="$(PYTHON)" \
		VERILATOR="$(VERILATOR)" VERILATOR_JOBS="$(VERILATOR_JOBS)" \
		RT11_IMAGE="$(abspath $(RT11_IMAGE))"

test-vendor: all
	$(MAKE) -C testbench am4-mcrom-ebr-test \
		ASM11="$(abspath $(ASM11))" PYTHON="$(PYTHON)" \
		IVERILOG="$(IVERILOG)" VVP="$(VVP)" \
		LATTICE_SIM_DIR="$(DIAMOND_HOME)/cae_library/simulation/verilog/machxo2"

project-test: all
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) -m unittest \
		testbench.test_am4_mcrom_ebr testbench.test_project

diamond: all
	@test -x "$(DIAMONDC)" || { echo "diamondc not found: $(DIAMONDC)"; exit 1; }
	cd $(BOARD) && LD_PRELOAD="$(DIAMOND_LIBSTDCPP)" \
		"$(DIAMONDC)" build-am4.tcl

$(XCF): $(JED) scripts/make_programmer_xcf.py | $(BUILD)
	$(PYTHON) scripts/make_programmer_xcf.py $(JED) $@

xcf: $(XCF)

program: $(XCF)
	@test -x "$(PGRCMD)" || { echo "pgrcmd not found: $(PGRCMD)"; exit 1; }
	"$(PGRCMD)" -infile "$(abspath $(XCF))"

clean:
	rm -rf $(BUILD) testbench/build
	$(MAKE) -C host/hg clean
	rm -f $(BOOT_BIN) $(BOARD)/am4_sd_boot.lst $(BOARD)/am4_sd_boot.asm.log
	rm -f $(SERVICE_BIN) $(BOARD)/am4_rk_service.lst $(BOARD)/am4_rk_service.asm.log
	rm -f $(BOOT_ROM) $(MICROM_EBR)

distclean: clean
	rm -rf $(BOARD)/impl1-am4

help:
	@echo "make                  assemble MicROM/firmware and generate seven-EBR ROM"
	@echo "make test-fast        core, EIS/FIS, interrupts, MOVB, FRAM, SD boot, ODT and Python checks"
	@echo "make test             test-fast plus RK611 READ/WRITE"
	@echo "make test-rt11        boot RT-11 image with Verilator"
	@echo "make test-rt11-ctrlc  run RT-11 MACRO/Ctrl-C regression with shortened SD delay"
	@echo "make test-rt11-ctrlc-full  run the same regression with the complete SD delay"
	@echo "make host-test        validate HG protocol, image and directory mirror"
	@echo "make test-vendor      verify generated ROM with Diamond DP8KC model"
	@echo "make diamond          synthesize/place/route/export JED with Diamond 3.14"
	@echo "make program          generate XCF and erase/program/verify HC1200"
