PYTHON ?= python3
IVERILOG ?= iverilog
VVP ?= vvp
VERILATOR ?= verilator
VERILATOR_JOBS ?= 4
ASM11 ?= ../../microasm11/microasm11
OD ?= od
RT11_IMAGE ?= ../images/rt11v503.dsk
LATTICE_SIM_DIR ?= $(HOME)/.local/lscc/diamond/3.14/cae_library/simulation/verilog/machxo2

AM4_CORE_RTL = ../rtl/experimental/am4/am4_direct.v \
	../rtl/experimental/am4/am4_alu.v \
	../rtl/experimental/am4/am4_seq.v \
	../rtl/experimental/am4/am4_plm.v \
	../rtl/experimental/am4/am4_mcrom.v

AM4_SYSTEM_RTL = ../boards/hc1200-microcomp/am4_cpu11_bus.v \
	../rtl/spi_byte_service.v ../rtl/spi_fram_guest_ram.v \
	spi_fram_model.v spi_sd_model.v $(AM4_CORE_RTL) \
	../rtl/experimental/lsi11/wbc_uart.v \
	../rtl/experimental/lsi11/wbc_uart_xo2.v

.PHONY: test test-fast-local test-local python-test am4-direct-test \
	am4-eis-test am4-fis-test am4-movb-bus-test am4-fram-test \
	am4-sd-boot-test am4-rk-test \
	am4-rk-test-icarus \
	am4-odt-test am4-rt11-boot-fast am4-rt11-ctrlc \
	am4-rt11-ctrlc-full am4-mcrom-ebr-test clean
.DELETE_ON_ERROR:

build:
	mkdir -p $@

build/%.bin: native_programs/%.asm | build
	@test -x "$(ASM11)" || { echo "microasm11 not found: $(ASM11)"; exit 1; }
	$(ASM11) -binary --cpu dcj-11 --list build/$*.lst $< $@ \
		> build/$*.asm.log 2>&1 || { cat build/$*.asm.log; exit 1; }

build/%.hex: build/%.bin
	{ printf '@0000\n'; $(OD) -An -tx1 -v $<; } > $@

build/am4_bootrom.rom: ../boards/hc1200-microcomp/am4_sd_boot.rom | build
	cp $< $@

test: test-local

test-fast-local: am4-direct-test am4-eis-test am4-fis-test \
	am4-movb-bus-test am4-fram-test am4-sd-boot-test am4-odt-test python-test

test-local: test-fast-local am4-rk-test

am4-direct-test: build/lsi11_original_smoke.hex build/am4_interrupt.hex
	$(IVERILOG) -g2012 -Wall -s tb_am4_direct -o build/am4_direct.vvp \
		tb_am4_direct.v $(AM4_CORE_RTL)
	$(VVP) build/am4_direct.vvp
	$(IVERILOG) -g2012 -Wall -s tb_am4_interrupt -o build/am4_interrupt.vvp \
		tb_am4_interrupt.v $(AM4_CORE_RTL)
	$(VVP) build/am4_interrupt.vvp

build/am4_fis.bin: native_programs/am4_fis_test.inc

build/am4_arith.vvp: tb_am4_arith.v $(AM4_CORE_RTL) | build
	$(IVERILOG) -g2012 -Wall -s tb_am4_arith -o $@ $< $(AM4_CORE_RTL)

am4-eis-test: build/am4_eis.hex build/am4_arith.vvp
	$(VVP) build/am4_arith.vvp +PROGRAM=build/am4_eis.hex

am4-fis-test: build/am4_fis.hex build/am4_arith.vvp
	$(VVP) build/am4_arith.vvp +PROGRAM=build/am4_fis.hex

am4-movb-bus-test: build/am4_movb_bus.hex
	$(IVERILOG) -g2012 -Wall -s tb_am4_movb_bus -o build/am4_movb_bus.vvp \
		tb_am4_movb_bus.v $(AM4_CORE_RTL)
	$(VVP) build/am4_movb_bus.vvp

am4-fram-test: build/am4_fram.hex
	$(IVERILOG) -g2012 -Wall -s tb_am4_fram_bus -o build/am4_fram_bus.vvp \
		tb_am4_fram_bus.v ../boards/hc1200-microcomp/am4_cpu11_bus.v \
		../rtl/spi_fram_guest_ram.v ../rtl/spi_byte_service.v spi_fram_model.v \
		../rtl/experimental/lsi11/wbc_uart.v \
		../rtl/experimental/lsi11/wbc_uart_xo2.v
	$(VVP) build/am4_fram_bus.vvp
	$(IVERILOG) -g2012 -Wall -s tb_am4_fram_execute -o build/am4_fram_execute.vvp \
		tb_am4_fram_execute.v $(AM4_SYSTEM_RTL)
	$(VVP) build/am4_fram_execute.vvp

am4-sd-boot-test: build/am4_sd_boot.hex build/am4_bootrom.rom
	$(IVERILOG) -g2012 -Wall -s tb_am4_sd_boot_execute \
		-o build/am4_sd_boot_execute.vvp tb_am4_sd_boot_execute.v $(AM4_SYSTEM_RTL)
	$(VVP) build/am4_sd_boot_execute.vvp
	$(VVP) build/am4_sd_boot_execute.vvp +FAIL_BOOT

build/am4_rk_verilator/Vtb_am4_rk_service: tb_am4_rk_service.v \
		$(AM4_SYSTEM_RTL) build/am4_rk_read.hex build/am4_bootrom.rom | build
	$(VERILATOR) --binary --timing -j $(VERILATOR_JOBS) -CFLAGS '-O3' \
		-Wno-fatal --top-module tb_am4_rk_service \
		--Mdir build/am4_rk_verilator $< $(AM4_SYSTEM_RTL) \
		> build/am4_rk_verilator.log 2>&1 || \
		{ cat build/am4_rk_verilator.log; exit 1; }

am4-rk-test: build/am4_rk_verilator/Vtb_am4_rk_service
	build/am4_rk_verilator/Vtb_am4_rk_service

am4-rk-test-icarus: build/am4_rk_read.hex build/am4_bootrom.rom
	$(IVERILOG) -g2012 -Wall -s tb_am4_rk_service \
		-o build/am4_rk_service.vvp tb_am4_rk_service.v $(AM4_SYSTEM_RTL)
	$(VVP) build/am4_rk_service.vvp

am4-odt-test: build/am4_bootrom.rom
	$(IVERILOG) -g2012 -Wall -s tb_am4_odt -o build/am4_odt.vvp \
		tb_am4_odt.v $(AM4_SYSTEM_RTL)
	$(VVP) build/am4_odt.vvp

build/am4_rt11_verilator/Vtb_am4_rt11: tb_am4_rt11.v $(AM4_SYSTEM_RTL) \
		build/am4_bootrom.rom | build
	$(VERILATOR) --binary --timing -j $(VERILATOR_JOBS) -CFLAGS '-O3' \
		-Wno-fatal --top-module tb_am4_rt11 \
		--Mdir build/am4_rt11_verilator $< $(AM4_SYSTEM_RTL) \
		> build/am4_rt11_verilator.log 2>&1 || \
		{ cat build/am4_rt11_verilator.log; exit 1; }

am4-rt11-boot-fast: build/am4_rt11_verilator/Vtb_am4_rt11
	@test -f "$(RT11_IMAGE)" || { echo "RT11_IMAGE does not exist: $(RT11_IMAGE)"; exit 1; }
	build/am4_rt11_verilator/Vtb_am4_rt11 +SD_IMAGE="$(RT11_IMAGE)"

build/am4_ctrlc_verilator/Vtb_am4_ctrlc: tb_am4_ctrlc.v $(AM4_SYSTEM_RTL) \
		build/am4_bootrom.rom | build
	$(VERILATOR) --binary --timing -j $(VERILATOR_JOBS) -CFLAGS '-O3' \
		-Wno-fatal --top-module tb_am4_ctrlc \
		--Mdir build/am4_ctrlc_verilator $< $(AM4_SYSTEM_RTL) \
		> build/am4_ctrlc_verilator.log 2>&1 || \
		{ cat build/am4_ctrlc_verilator.log; exit 1; }

am4-rt11-ctrlc: build/am4_ctrlc_verilator/Vtb_am4_ctrlc
	@test -f "$(RT11_IMAGE)" || { echo "RT11_IMAGE does not exist: $(RT11_IMAGE)"; exit 1; }
	build/am4_ctrlc_verilator/Vtb_am4_ctrlc \
		+SD_IMAGE="$(RT11_IMAGE)" +FAST_BUSY_WAIT

am4-rt11-ctrlc-full: build/am4_ctrlc_verilator/Vtb_am4_ctrlc
	@test -f "$(RT11_IMAGE)" || { echo "RT11_IMAGE does not exist: $(RT11_IMAGE)"; exit 1; }
	build/am4_ctrlc_verilator/Vtb_am4_ctrlc +SD_IMAGE="$(RT11_IMAGE)"

am4-mcrom-ebr-test: build/am4_bootrom.rom
	@test -f "$(LATTICE_SIM_DIR)/DP8KC.v" || \
		{ echo "Diamond DP8KC model not found under $(LATTICE_SIM_DIR)"; exit 1; }
	$(IVERILOG) -g2012 -Wall -s tb_am4_mcrom_ebr -y $(LATTICE_SIM_DIR) \
		-o build/am4_mcrom_ebr.vvp tb_am4_mcrom_ebr.v \
		../boards/hc1200-microcomp/am4_mcrom_ebr.v
	$(VVP) build/am4_mcrom_ebr.vvp

python-test:
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) -m unittest \
		test_am4_mcrom_ebr test_project

clean:
	rm -rf build
