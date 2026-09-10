PYTHON ?= python3
IVERILOG ?= iverilog
VVP ?= vvp
VERILATOR ?= verilator
DIAMOND_HOME ?= $(HOME)/.local/lscc/diamond/3.14
DIAMOND_LIBSTDCPP ?= /lib/x86_64-linux-gnu/libstdc++.so.6
SYNTH_MHZ ?= 35
LATTICE_SIM_DIR ?= $(DIAMOND_HOME)/cae_library/simulation/verilog/machxo2
GEN = microcode/generated
ENGINE = rtl/uj11_engine.v rtl/uj11_microseq.v rtl/uj11_datapath.v rtl/uj11_regfile.v rtl/uj11_alu.v rtl/uj11_psw.v rtl/uj11_mem.v
CORE = rtl/uj11_core.v rtl/uj11_decode.v $(ENGINE)
FRAM = rtl/uj11_fram_system.v rtl/uj11_fram_baseline.v rtl/uj11_fram_transport.v rtl/uj11_prefetch.v rtl/uj11_stream.v rtl/uj11_prefetch_control.v reference/lsi11/spi_fram_guest_ram.v
FRAM_MODEL = reference/lsi11/spi_fram_model.v

.PHONY: all microcode microcode-m0 test test-python test-rtl test-datapath test-engine test-memory test-memory-engine test-decode test-core lint vendor-test vendor-engine vendor-memory-engine vendor-core benchmark vendor-benchmark synthesis synthesis-seq audit manifest clean
.PHONY: test-fram test-fram-core test-stream test-lsi11-io benchmark-fram vendor-fram vendor-fram-benchmark synthesis-fram
all: microcode microcode-m0

.PHONY: test-isa verify-cp16
test: test-isa test-bus-fault test-bus-fault-system test-bus-fault-double
verify-cp16:
	$(PYTHON) tools/verify_cp16.py

microcode: $(GEN)/checkpoint_seq.mem $(GEN)/uj11_rom_ebr.v

$(GEN)/checkpoint_seq.mem: microcode/checkpoint_seq.uasm microasm/uj11asm.py
	$(PYTHON) microasm/uj11asm.py $< -o $@ --list $(GEN)/checkpoint_seq.lst --labels $(GEN)/checkpoint_seq.labels.json --stats $(GEN)/checkpoint_seq.stats.json

$(GEN)/uj11_rom_ebr.v: $(GEN)/checkpoint_seq.mem tools/make_ebr.py
	$(PYTHON) tools/make_ebr.py $< $@

test-isa: test-python test-rtl test-datapath test-engine test-memory test-memory-engine test-decode test-core lint test-fram test-ea test-single test-branch test-byte-primitives test-byte test-single-byte test-control test-control-faults test-extra test-extra-faults test-trap test-trap-faults test-irq test-irq-adapter test-irq-lsi11 test-irq-faults test-illegal test-illegal-faults

test-python:
	$(PYTHON) -m unittest discover -s tb -p 'test_*.py' -v

build:
	mkdir -p build

test-rtl: microcode | build
	$(IVERILOG) -g2012 -Wall -s tb_microseq -o build/seq tb/tb_microseq.v rtl/uj11_microseq.v
	$(VVP) build/seq
	$(IVERILOG) -g2012 -Wall -s tb_rom -o build/rom tb/tb_rom.v rtl/uj11_rom.v
	$(VVP) build/rom
	$(IVERILOG) -g2012 -Wall -s tb_control_store -o build/store tb/tb_control_store.v rtl/uj11_microseq.v rtl/uj11_rom.v
	$(VVP) build/store

lint: microcode
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_seq synth/machxo2/uj11_probe_seq.v rtl/uj11_microseq.v rtl/uj11_rom.v
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_datapath synth/machxo2/uj11_probe_datapath.v rtl/uj11_datapath.v rtl/uj11_regfile.v rtl/uj11_alu.v

test-datapath: | build
	$(IVERILOG) -g2012 -Wall -s tb_alu -o build/alu tb/tb_alu.v rtl/uj11_alu.v
	$(VVP) build/alu
	$(IVERILOG) -g2012 -Wall -s tb_datapath -o build/datapath tb/tb_datapath.v rtl/uj11_alu.v rtl/uj11_regfile.v rtl/uj11_datapath.v
	$(VVP) build/datapath

microcode-m0: $(GEN)/m0.mem $(GEN)/uj11_m0_ebr.v

$(GEN)/m0.mem: microcode/m0.uasm microcode/fis.uasm tools/link_fis.py microasm/uj11asm.py
	$(PYTHON) tools/link_fis.py --output $(GEN)/full.uasm
	$(PYTHON) microasm/uj11asm.py $(GEN)/full.uasm -o $@ --list $(GEN)/m0.lst --labels $(GEN)/m0.labels.json --stats $(GEN)/m0.stats.json

$(GEN)/uj11_m0_ebr.v: $(GEN)/m0.mem tools/make_ebr.py
	$(PYTHON) tools/make_ebr.py $< $@

test-engine: microcode-m0 | build
	$(IVERILOG) -g2012 -Wall -s tb_engine -o build/engine tb/tb_engine.v $(ENGINE) rtl/uj11_rom.v
	$(VVP) build/engine
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_engine synth/machxo2/uj11_probe_engine.v $(ENGINE) rtl/uj11_rom.v

vendor-engine: microcode-m0 | build
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_engine -o build/vendor_engine tb/tb_engine.v $(ENGINE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_engine

test-memory: | build
	$(IVERILOG) -g2012 -Wall -s tb_mem_psw -o build/mem_psw tb/tb_mem_psw.v tb/uj11_ram.v rtl/uj11_mem.v rtl/uj11_psw.v
	$(VVP) build/mem_psw

$(GEN)/memory_test.mem: microcode/memory_test.uasm microasm/uj11asm.py
	$(PYTHON) microasm/uj11asm.py $< -o $@

$(GEN)/uj11_memory_test_ebr.v: $(GEN)/memory_test.mem tools/make_ebr.py
	$(PYTHON) tools/make_ebr.py $< $@

test-memory-engine: $(GEN)/memory_test.mem | build
	$(IVERILOG) -g2012 -Wall -s tb_memory_engine -o build/memory_engine tb/tb_memory_engine.v tb/uj11_ram.v $(ENGINE) rtl/uj11_rom.v
	$(VVP) build/memory_engine

vendor-memory-engine: $(GEN)/uj11_memory_test_ebr.v | build
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_memory_engine -o build/vendor_memory_engine tb/tb_memory_engine.v tb/uj11_ram.v $(ENGINE) $(GEN)/uj11_memory_test_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_memory_engine

test-decode: | build
	$(IVERILOG) -g2012 -Wall -s tb_decode -o build/decode tb/tb_decode.v rtl/uj11_decode.v
	$(VVP) build/decode
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_decode synth/machxo2/uj11_probe_decode.v rtl/uj11_decode.v

build/reference_vectors: tb/reference_vectors.c ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. tb/reference_vectors.c ../core/core.c ../core/hardware.c -lm -o $@

build/isa_vectors.mem: build/reference_vectors
	$< > $@.tmp
	mv $@.tmp $@

test-core: microcode-m0 build/isa_vectors.mem
	$(IVERILOG) -g2012 -Wall -s tb_core -o build/core tb/tb_core.v tb/uj11_ram.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/core
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_core synth/machxo2/uj11_probe_core.v $(CORE) rtl/uj11_rom.v

vendor-core: microcode-m0 build/isa_vectors.mem
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_core -o build/vendor_core tb/tb_core.v tb/uj11_ram.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_core

test-fram: test-fram-core test-stream test-lsi11-io
	$(IVERILOG) -g2012 -Wall -s tb_prefetch -o build/prefetch tb/tb_prefetch.v rtl/uj11_prefetch.v rtl/uj11_fram_transport.v $(FRAM_MODEL)
	$(VVP) build/prefetch
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_fram_pf synth/machxo2/uj11_probe_fram_pf.v synth/machxo2/uj11_probe_fram.v $(CORE) $(FRAM) rtl/uj11_rom.v
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_fram_seq synth/machxo2/uj11_probe_fram_seq.v synth/machxo2/uj11_probe_fram.v $(CORE) $(FRAM) rtl/uj11_rom.v

test-fram-core: microcode-m0 build/isa_vectors.mem
	$(IVERILOG) -g2012 -Wall -s tb_core_fram -o build/core_fram tb/tb_core_fram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v
	$(VVP) build/core_fram

$(GEN)/stream_test.mem: microcode/stream_test.uasm microasm/uj11asm.py
	$(PYTHON) microasm/uj11asm.py $< -o $@

$(GEN)/uj11_stream_test_ebr.v: $(GEN)/stream_test.mem tools/make_ebr.py
	$(PYTHON) tools/make_ebr.py $< $@

test-stream: $(GEN)/stream_test.mem | build
	$(IVERILOG) -g2012 -Wall -s tb_stream_engine -o build/stream_engine tb/tb_stream_engine.v $(ENGINE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v
	$(VVP) build/stream_engine
	$(IVERILOG) -g2012 -Wall -s tb_stream_hint -o build/stream_hint tb/tb_stream_hint.v rtl/uj11_stream.v
	$(VVP) build/stream_hint
	$(IVERILOG) -g2012 -Wall -s tb_prefetch_control -o build/prefetch_control tb/tb_prefetch_control.v rtl/uj11_prefetch_control.v
	$(VVP) build/prefetch_control

test-lsi11-io: | build
	$(IVERILOG) -g2012 -Wall -Wno-timescale -s tb_lsi11_io -o build/lsi11_io tb/tb_lsi11_io.v rtl/uj11_prefetch.v rtl/uj11_fram_transport.v reference/lsi11/*.v
	$(VVP) build/lsi11_io

vendor-fram: microcode-m0 $(GEN)/uj11_stream_test_ebr.v build/isa_vectors.mem
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_core_fram -o build/vendor_core_fram tb/tb_core_fram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_core_fram
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_stream_engine -o build/vendor_stream_engine tb/tb_stream_engine.v $(ENGINE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_stream_test_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_stream_engine

benchmark-fram: microcode-m0 | build
	@for mode in 0 1 2; do \
	  $(IVERILOG) -g2012 -Wall -s tb_fram_bench -Ptb_fram_bench.MEMORY_MODE=$$mode -o build/fram_bench_$$mode tb/tb_fram_bench.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/fram_bench_$$mode || exit 1; done

vendor-fram-benchmark: microcode-m0 | build
	@for mode in 0 1 2; do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_fram_bench -Ptb_fram_bench.MEMORY_MODE=$$mode -o build/vendor_fram_bench_$$mode tb/tb_fram_bench.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_fram_bench_$$mode || exit 1; done

synthesis-fram: microcode-m0
	$(PYTHON) tools/checkpoint.py cp27b --mhz 29.56

benchmark: microcode-m0 | build
	$(IVERILOG) -g2012 -Wall -s tb_bench -o build/bench tb/tb_bench.v tb/uj11_ram.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/bench

vendor-benchmark: microcode-m0 | build
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_bench -o build/vendor_bench tb/tb_bench.v tb/uj11_ram.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_bench

vendor-test: microcode | build
	test -f "$(LATTICE_SIM_DIR)/DP8KC.v"
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_rom -o build/vendor_rom tb/tb_rom.v $(GEN)/uj11_rom_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_rom
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_control_store -o build/vendor_store tb/tb_control_store.v rtl/uj11_microseq.v $(GEN)/uj11_rom_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_store

manifest: microcode | build
	$(PYTHON) tools/source_manifest.py build/cp1-inputs.json

synthesis: microcode-m0
	DIAMOND_HOME="$(DIAMOND_HOME)" DIAMOND_LIBSTDCPP="$(DIAMOND_LIBSTDCPP)" $(PYTHON) tools/checkpoint.py cp27a --mhz $(SYNTH_MHZ)

synthesis-seq: manifest
	@test -x "$(DIAMOND_HOME)/bin/lin64/diamondc" || { echo 'Diamond unavailable: set DIAMOND_HOME or run on the existing Linux Diamond host.'; exit 1; }
	LD_PRELOAD="$(DIAMOND_LIBSTDCPP)" "$(DIAMOND_HOME)/bin/lin64/diamondc" synth/machxo2/build.tcl
	$(PYTHON) tools/report_synthesis.py synth/machxo2/impl1/uj11-seq_impl1.mrp synth/machxo2/impl1/uj11-seq_impl1.twr --output build/cp1-synthesis.json

audit:
	$(PYTHON) tools/audit_previous.py

clean:
	rm -rf build
	rm -f $(GEN)/*.mem $(GEN)/*.lst $(GEN)/*.json $(GEN)/*.v

.PHONY: test-ea vendor-ea benchmark-ea vendor-ea-benchmark
EA_MODES ?= -1 0 1 2
EA_TEST_MODES ?= -1 2
build/ea_vectors: tb/ea_vectors.c tb/trace_oracle.h ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. tb/ea_vectors.c ../core/core.c ../core/hardware.c -lm -o $@

build/ea-vectors.txt: build/ea_vectors
	$< > $@.tmp 2> build/ea-oracle.log
	mv $@.tmp $@

test-ea: microcode-m0 build/ea-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea -Ptb_ea.MEMORY_MODE=$$mode -o build/ea_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/ea_$$mode || exit 1; done
	$(IVERILOG) -g2012 -Wall -s tb_ea_faults -o build/ea_faults tb/tb_ea_faults.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/ea_faults

vendor-ea: microcode-m0 build/ea-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea -Ptb_ea.MEMORY_MODE=$$mode -o build/vendor_ea_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_ea_$$mode || exit 1; done
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea_faults -o build/vendor_ea_faults tb/tb_ea_faults.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_ea_faults

benchmark-ea: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea_bench -Ptb_ea_bench.MEMORY_MODE=$$mode -o build/ea_bench$$mode tb/tb_ea_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/ea_bench$$mode || exit 1; done

vendor-ea-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea_bench -Ptb_ea_bench.MEMORY_MODE=$$mode -o build/vendor_ea_bench$$mode tb/tb_ea_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_ea_bench$$mode || exit 1; done

.PHONY: test-single vendor-single
build/single_vectors: tb/single_vectors.c tb/trace_oracle.h ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. tb/single_vectors.c ../core/core.c ../core/hardware.c -lm -o $@

build/single-vectors.txt: build/single_vectors
	$< > $@.tmp 2> build/single-oracle.log
	mv $@.tmp $@

test-single: microcode-m0 build/single-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea -Ptb_ea.SUITE=1 -Ptb_ea.MEMORY_MODE=$$mode -o build/single_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/single_$$mode || exit 1; done
	$(IVERILOG) -g2012 -Wall -s tb_single_faults -o build/single_faults tb/tb_single_faults.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/single_faults

vendor-single: microcode-m0 build/single-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea -Ptb_ea.SUITE=1 -Ptb_ea.MEMORY_MODE=$$mode -o build/vendor_single_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_single_$$mode || exit 1; done

	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_single_faults -o build/vendor_single_faults tb/tb_single_faults.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_single_faults
.PHONY: test-branch vendor-branch
build/branch_vectors: tb/branch_vectors.c tb/trace_oracle.h ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. tb/branch_vectors.c ../core/core.c ../core/hardware.c -lm -o $@

build/branch-vectors.txt: build/branch_vectors
	$< > $@.tmp 2> build/branch-oracle.log
	mv $@.tmp $@

test-branch: microcode-m0 build/branch-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea -Ptb_ea.SUITE=2 -Ptb_ea.MEMORY_MODE=$$mode -o build/branch_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/branch_$$mode || exit 1; done

vendor-branch: microcode-m0 build/branch-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea -Ptb_ea.SUITE=2 -Ptb_ea.MEMORY_MODE=$$mode -o build/vendor_branch_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_branch_$$mode || exit 1; done


.PHONY: benchmark-cp9 vendor-cp9-benchmark

benchmark-cp9: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_cp9_bench -Ptb_cp9_bench.MEMORY_MODE=$$mode -o build/cp9_bench$$mode tb/tb_cp9_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/cp9_bench$$mode || exit 1; done

vendor-cp9-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_cp9_bench -Ptb_cp9_bench.MEMORY_MODE=$$mode -o build/vendor_cp9_bench$$mode tb/tb_cp9_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_cp9_bench$$mode || exit 1; done

.PHONY: test-byte-primitives test-movb vendor-movb

test-byte-primitives: | build
	$(IVERILOG) -g2012 -Wall -s tb_alu_byte -o build/alu_byte tb/tb_alu_byte.v rtl/uj11_alu.v
	$(VVP) build/alu_byte
	$(IVERILOG) -g2012 -Wall -s tb_datapath_byte -o build/datapath_byte tb/tb_datapath_byte.v rtl/uj11_datapath.v rtl/uj11_alu.v rtl/uj11_regfile.v
	$(VVP) build/datapath_byte

build/byte_vectors: tb/byte_vectors.c tb/trace_oracle.h build/oracle_core.c ../core/core.h ../core/pdp11_fp.c ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DENABLE_MMU=0 -I.. -I../core tb/byte_vectors.c build/oracle_core.c ../core/hardware.c -lm -o $@

build/movb-vectors.txt: build/byte_vectors
	$< 1 > $@.tmp 2> build/movb-oracle.log
	mv $@.tmp $@

test-movb: microcode-m0 build/movb-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea -Ptb_ea.SUITE=3 -Ptb_ea.MEMORY_MODE=$$mode -o build/movb_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/movb_$$mode || exit 1; done

vendor-movb: microcode-m0 build/movb-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea -Ptb_ea.SUITE=3 -Ptb_ea.MEMORY_MODE=$$mode -o build/vendor_movb_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_movb_$$mode || exit 1; done

.PHONY: benchmark-movb vendor-movb-benchmark

benchmark-movb: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea_bench -Ptb_ea_bench.BYTE_VARIANT=1 -Ptb_ea_bench.MOVB_ONLY=1 -Ptb_ea_bench.MEMORY_MODE=$$mode -o build/movb_bench$$mode tb/tb_ea_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/movb_bench$$mode || exit 1; done

vendor-movb-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea_bench -Ptb_ea_bench.BYTE_VARIANT=1 -Ptb_ea_bench.MOVB_ONLY=1 -Ptb_ea_bench.MEMORY_MODE=$$mode -o build/vendor_movb_bench$$mode tb/tb_ea_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_movb_bench$$mode || exit 1; done

.PHONY: test-byte vendor-byte test-single-byte vendor-single-byte benchmark-byte vendor-byte-benchmark
build/byte-vectors.txt: build/byte_vectors
	$< 5 > $@.tmp 2> build/byte-oracle.log
	mv $@.tmp $@

test-byte: microcode-m0 build/byte-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea -Ptb_ea.SUITE=4 -Ptb_ea.MEMORY_MODE=$$mode -o build/byte_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/byte_$$mode || exit 1; done
	$(IVERILOG) -g2012 -Wall -s tb_byte_io -o build/byte_io tb/tb_byte_io.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/byte_io

vendor-byte: microcode-m0 build/byte-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea -Ptb_ea.SUITE=4 -Ptb_ea.MEMORY_MODE=$$mode -o build/vendor_byte_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_byte_$$mode || exit 1; done
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_byte_io -o build/vendor_byte_io tb/tb_byte_io.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_byte_io

build/single_byte_vectors: tb/single_vectors.c tb/trace_oracle.h build/oracle_core.c ../core/core.h ../core/pdp11_fp.c ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DBYTE_VARIANT=1 -DORACLE_ACCESS_AUDIT=1 -DENABLE_MMU=0 -I.. -I../core tb/single_vectors.c build/oracle_core.c ../core/hardware.c -lm -o $@

build/single_byte-vectors.txt: build/single_byte_vectors
	$< > $@.tmp 2> build/single_byte-oracle.log
	mv $@.tmp $@

test-single-byte: microcode-m0 build/single_byte-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea -Ptb_ea.SUITE=5 -Ptb_ea.MEMORY_MODE=$$mode -o build/single_byte_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/single_byte_$$mode || exit 1; done
	$(IVERILOG) -g2012 -Wall -Ptb_single_faults.BYTE_VARIANT=1 -s tb_single_faults -o build/single_byte_faults tb/tb_single_faults.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/single_byte_faults

vendor-single-byte: microcode-m0 build/single_byte-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea -Ptb_ea.SUITE=5 -Ptb_ea.MEMORY_MODE=$$mode -o build/vendor_single_byte_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_single_byte_$$mode || exit 1; done

	$(IVERILOG) -g2012 -DVENDOR_ROM -Ptb_single_faults.BYTE_VARIANT=1 -s tb_single_faults -o build/vendor_single_byte_faults tb/tb_single_faults.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_single_byte_faults
benchmark-byte: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea_bench -Ptb_ea_bench.BYTE_VARIANT=1 -Ptb_ea_bench.MEMORY_MODE=$$mode -o build/byte_bench$$mode tb/tb_ea_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/byte_bench$$mode || exit 1; done
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_cp9_bench -Ptb_cp9_bench.BYTE_VARIANT=1 -Ptb_cp9_bench.MEMORY_MODE=$$mode -o build/single_byte_bench$$mode tb/tb_cp9_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/single_byte_bench$$mode || exit 1; done


vendor-byte-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea_bench -Ptb_ea_bench.BYTE_VARIANT=1 -Ptb_ea_bench.MEMORY_MODE=$$mode -o build/vendor_byte_bench$$mode tb/tb_ea_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_byte_bench$$mode || exit 1; done

	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_cp9_bench -Ptb_cp9_bench.BYTE_VARIANT=1 -Ptb_cp9_bench.MEMORY_MODE=$$mode -o build/vendor_single_byte_bench$$mode tb/tb_cp9_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_single_byte_bench$$mode || exit 1; done

build/oracle_core.c: ../core/core.c tools/oracle_core.py | build
	$(PYTHON) tools/oracle_core.py

.PHONY: test-control vendor-control
build/oracle_control_core.c: ../core/core.c tools/oracle_core.py | build
	$(PYTHON) tools/oracle_core.py --vectors

build/control_vectors: tb/control_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.h ../core/pdp11_fp.c ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DENABLE_MMU=0 -I.. -I../core tb/control_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@

build/control-vectors.txt: build/control_vectors
	$< > $@.tmp 2> build/control-oracle.log
	mv $@.tmp $@

test-control: microcode-m0 build/control-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea -Ptb_ea.SUITE=6 -Ptb_ea.MEMORY_MODE=$$mode -o build/control_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/control_$$mode || exit 1; done

vendor-control: microcode-m0 build/control-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea -Ptb_ea.SUITE=6 -Ptb_ea.MEMORY_MODE=$$mode -o build/vendor_control_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_control_$$mode || exit 1; done

.PHONY: benchmark-control vendor-control-benchmark
benchmark-control: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_control_bench -Ptb_control_bench.MEMORY_MODE=$$mode -o build/control_bench$$mode tb/tb_control_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/control_bench$$mode || exit 1; done

vendor-control-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_control_bench -Ptb_control_bench.MEMORY_MODE=$$mode -o build/vendor_control_bench$$mode tb/tb_control_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_control_bench$$mode || exit 1; done

.PHONY: test-control-faults vendor-control-faults
test-control-faults: microcode-m0
	$(IVERILOG) -g2012 -Wall -s tb_control_faults -o build/control_faults tb/tb_control_faults.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/control_faults
vendor-control-faults: microcode-m0
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_control_faults -o build/vendor_control_faults tb/tb_control_faults.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_control_faults

.PHONY: test-extra vendor-extra
build/extra_vectors: tb/extra_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.h ../core/pdp11_fp.c ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DENABLE_MMU=0 -I.. -I../core tb/extra_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@

build/extra-vectors.txt: build/extra_vectors
	$< > $@.tmp 2> build/extra-oracle.log
	mv $@.tmp $@

test-extra: microcode-m0 build/extra-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea -Ptb_ea.SUITE=7 -Ptb_ea.MEMORY_MODE=$$mode -o build/extra_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/extra_$$mode || exit 1; done

vendor-extra: microcode-m0 build/extra-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea -Ptb_ea.SUITE=7 -Ptb_ea.MEMORY_MODE=$$mode -o build/vendor_extra_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_extra_$$mode || exit 1; done


test-extra-faults: microcode-m0
	$(IVERILOG) -g2012 -Wall -s tb_extra_faults -o build/extra_faults tb/tb_extra_faults.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/extra_faults
vendor-extra-faults: microcode-m0
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_extra_faults -o build/vendor_extra_faults tb/tb_extra_faults.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_extra_faults


benchmark-extra: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_extra_bench -Ptb_extra_bench.MEMORY_MODE=$$mode -o build/extra_bench$$mode tb/tb_extra_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/extra_bench$$mode || exit 1; done

vendor-extra-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_extra_bench -Ptb_extra_bench.MEMORY_MODE=$$mode -o build/vendor_extra_bench$$mode tb/tb_extra_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_extra_bench$$mode || exit 1; done

.PHONY: test-extra-faults vendor-extra-faults benchmark-extra vendor-extra-benchmark verify-cp12
verify-cp12:
	$(PYTHON) tools/verify_cp12.py

.PHONY: test-trap vendor-trap
build/trap_vectors: tb/trap_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.h ../core/pdp11_fp.c ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/trap_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@

build/trap-vectors.txt: build/trap_vectors
	$< > $@.tmp 2> build/trap-oracle.log
	mv $@.tmp $@

test-trap: microcode-m0 build/trap-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea -Ptb_ea.SUITE=8 -Ptb_ea.MEMORY_MODE=$$mode -o build/trap_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/trap_$$mode || exit 1; done

vendor-trap: microcode-m0 build/trap-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea -Ptb_ea.SUITE=8 -Ptb_ea.MEMORY_MODE=$$mode -o build/vendor_trap_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_trap_$$mode || exit 1; done


.PHONY: test-trap-faults vendor-trap-faults
test-trap-faults: microcode-m0
	$(IVERILOG) -g2012 -Wall -s tb_trap_faults -o build/trap_faults tb/tb_trap_faults.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/trap_faults
vendor-trap-faults: microcode-m0
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trap_faults -o build/vendor_trap_faults tb/tb_trap_faults.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_trap_faults


.PHONY: benchmark-trap vendor-trap-benchmark
benchmark-trap: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_trap_bench -Ptb_trap_bench.MEMORY_MODE=$$mode -o build/trap_bench$$mode tb/tb_trap_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/trap_bench$$mode || exit 1; done

vendor-trap-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trap_bench -Ptb_trap_bench.MEMORY_MODE=$$mode -o build/vendor_trap_bench$$mode tb/tb_trap_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_trap_bench$$mode || exit 1; done


.PHONY: verify-cp13
verify-cp13:
	$(PYTHON) tools/verify_cp13.py

.PHONY: test-irq vendor-irq
build/irq_vectors: tb/irq_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.h ../core/pdp11_fp.c ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DORACLE_IRQ_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/irq_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@

build/irq-vectors.txt: build/irq_vectors
	$< > $@.tmp 2> build/irq-oracle.log
	mv $@.tmp $@

test-irq: microcode-m0 build/irq-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea -Ptb_ea.SUITE=9 -Ptb_ea.MEMORY_MODE=$$mode -o build/irq_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/irq_$$mode || exit 1; done

vendor-irq: microcode-m0 build/irq-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea -Ptb_ea.SUITE=9 -Ptb_ea.MEMORY_MODE=$$mode -o build/vendor_irq_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_irq_$$mode || exit 1; done

.PHONY: test-irq-adapter
test-irq-adapter: | build
	$(IVERILOG) -g2012 -Wall -s tb_irq_adapter -o build/irq_adapter tb/tb_irq_adapter.v rtl/uj11_irq_lsi11.v
	$(VVP) build/irq_adapter
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_fram_irq synth/machxo2/uj11_probe_fram_irq.v $(CORE) $(FRAM) rtl/uj11_irq_lsi11.v rtl/uj11_rom.v

.PHONY: test-irq-lsi11 vendor-irq-lsi11
IRQ_LEGACY = reference/lsi11/am4_cpu11_bus.v reference/lsi11/wbc_uart_xo2.v reference/lsi11/spi_byte_service.v
test-irq-lsi11: microcode-m0 | build
	$(IVERILOG) -g2012 -Wall -s tb_irq_lsi11 -o build/irq_lsi11 tb/tb_irq_lsi11.v $(CORE) $(FRAM) $(FRAM_MODEL) $(IRQ_LEGACY) rtl/uj11_irq_lsi11.v rtl/uj11_rom.v
	$(VVP) build/irq_lsi11
vendor-irq-lsi11: microcode-m0 | build
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_irq_lsi11 -o build/vendor_irq_lsi11 tb/tb_irq_lsi11.v $(CORE) $(FRAM) $(FRAM_MODEL) $(IRQ_LEGACY) rtl/uj11_irq_lsi11.v $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_irq_lsi11

.PHONY: test-irq-faults vendor-irq-faults
test-irq-faults: microcode-m0
	$(IVERILOG) -g2012 -Wall -s tb_irq_faults -o build/irq_faults tb/tb_irq_faults.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/irq_faults
vendor-irq-faults: microcode-m0
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_irq_faults -o build/vendor_irq_faults tb/tb_irq_faults.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_irq_faults



.PHONY: benchmark-irq vendor-irq-benchmark
benchmark-irq: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_irq_bench -Ptb_irq_bench.MEMORY_MODE=$$mode -o build/irq_bench$$mode tb/tb_irq_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/irq_bench$$mode || exit 1; done

vendor-irq-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_irq_bench -Ptb_irq_bench.MEMORY_MODE=$$mode -o build/vendor_irq_bench$$mode tb/tb_irq_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_irq_bench$$mode || exit 1; done



.PHONY: verify-cp14
verify-cp14:
	$(PYTHON) tools/verify_cp14.py

.PHONY: test-illegal vendor-illegal
build/illegal_vectors: tb/illegal_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.h ../core/pdp11_fp.c ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/illegal_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@

build/illegal-vectors.txt: build/illegal_vectors build/illegal-opcodes.txt
	$< > $@.tmp 2> build/illegal-oracle.log
	mv $@.tmp $@

test-illegal: microcode-m0 build/illegal-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_ea -Ptb_ea.SUITE=10 -Ptb_ea.MEMORY_MODE=$$mode -o build/illegal_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/illegal_$$mode || exit 1; done

vendor-illegal: microcode-m0 build/illegal-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_ea -Ptb_ea.SUITE=10 -Ptb_ea.MEMORY_MODE=$$mode -o build/vendor_illegal_$$mode tb/tb_ea.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_illegal_$$mode || exit 1; done



build/illegal-opcodes.txt: tb/tb_decode.v rtl/uj11_decode.v | build
	$(IVERILOG) -g2012 -Wall -s tb_decode -Ptb_decode.EMIT_CANDIDATES=1 -o build/illegal_decode tb/tb_decode.v rtl/uj11_decode.v
	$(VVP) build/illegal_decode

.PHONY: test-illegal-faults vendor-illegal-faults
test-illegal-faults: microcode-m0 | build
	$(IVERILOG) -g2012 -Wall -s tb_illegal_faults -o build/illegal_faults tb/tb_illegal_faults.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/illegal_faults
vendor-illegal-faults: microcode-m0 | build
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_illegal_faults -o build/vendor_illegal_faults tb/tb_illegal_faults.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_illegal_faults


.PHONY: benchmark-illegal vendor-illegal-benchmark
benchmark-illegal: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_illegal_bench -Ptb_illegal_bench.MEMORY_MODE=$$mode -o build/illegal_bench$$mode tb/tb_illegal_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/illegal_bench$$mode || exit 1; done

vendor-illegal-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_illegal_bench -Ptb_illegal_bench.MEMORY_MODE=$$mode -o build/vendor_illegal_bench$$mode tb/tb_illegal_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_illegal_bench$$mode || exit 1; done



.PHONY: verify-cp15
verify-cp15:
	$(PYTHON) tools/verify_cp15.py

.PHONY: test-bus-fault vendor-bus-fault
build/oracle_fault_core.c: ../core/core.c tools/fault_oracle.py tools/oracle_core.py | build
	$(PYTHON) tools/fault_oracle.py
build/bus_fault_vectors: tb/bus_fault_vectors.c build/oracle_fault_core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. -I../core tb/bus_fault_vectors.c build/oracle_fault_core.c ../core/hardware.c -lm -o $@
build/bus-fault-vectors.txt: build/bus_fault_vectors
	$< > $@.tmp 2> build/bus-fault-oracle.log
	mv $@.tmp $@
test-bus-fault: microcode-m0 build/bus-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_bus_fault -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/bus_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/bus_fault_$$mode || exit 1; done
vendor-bus-fault: microcode-m0 build/bus-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_bus_fault -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/vendor_bus_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_bus_fault_$$mode || exit 1; done
.PHONY: test-bus-fault-system vendor-bus-fault-system
test-bus-fault-system: microcode-m0 | build
	$(IVERILOG) -g2012 -Wall -s tb_bus_fault_system -o build/bus_fault_system tb/tb_bus_fault_system.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v
	$(VVP) build/bus_fault_system
vendor-bus-fault-system: microcode-m0 | build
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_bus_fault_system -o build/vendor_bus_fault_system tb/tb_bus_fault_system.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_bus_fault_system
.PHONY: test-bus-fault-double vendor-bus-fault-double
test-bus-fault-double: microcode-m0 | build
	$(IVERILOG) -g2012 -Wall -s tb_bus_fault_double -o build/bus_fault_double tb/tb_bus_fault_double.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/bus_fault_double
vendor-bus-fault-double: microcode-m0 | build
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_bus_fault_double -o build/vendor_bus_fault_double tb/tb_bus_fault_double.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_bus_fault_double
.PHONY: test-fault-irq-order
build/fault_irq_order: tb/fault_irq_order.c ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. tb/fault_irq_order.c ../core/core.c ../core/hardware.c -lm -o $@
test-fault-irq-order: build/fault_irq_order
	$<
test: test-fault-irq-order

.PHONY: test-trace-bit vendor-trace-bit
build/trace_bit_vectors: tb/trace_bit_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DORACLE_IRQ_PROFILE=1 -DORACLE_TRACE_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/trace_bit_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@
build/trace_bit-vectors.txt: build/trace_bit_vectors
	$< > $@.tmp 2> build/trace_bit-oracle.log
	mv $@.tmp $@
test-trace-bit: microcode-m0 build/trace_bit-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_trace_bit -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/trace_bit_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/trace_bit_$$mode || exit 1; done
vendor-trace-bit: microcode-m0 build/trace_bit-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trace_bit -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/vendor_trace_bit_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_trace_bit_$$mode || exit 1; done
.PHONY: test-trace-system vendor-trace-system
test-trace-system: microcode-m0 | build
	$(IVERILOG) -g2012 -Wall -s tb_trace_system -o build/trace_system tb/tb_trace_system.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v
	$(VVP) build/trace_system
vendor-trace-system: microcode-m0 | build
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trace_system -o build/vendor_trace_system tb/tb_trace_system.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_trace_system
.PHONY: test-trace-faults vendor-trace-faults
test-trace-faults: microcode-m0 | build
	$(IVERILOG) -g2012 -Wall -s tb_trace_faults -o build/trace_faults tb/tb_trace_faults.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/trace_faults
vendor-trace-faults: microcode-m0 | build
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trace_faults -o build/vendor_trace_faults tb/tb_trace_faults.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_trace_faults
.PHONY: benchmark-trace vendor-trace-benchmark
benchmark-trace: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_trace_bench -Ptb_trace_bench.MEMORY_MODE=$$mode -o build/trace_bench_$$mode tb/tb_trace_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/trace_bench_$$mode || exit 1; done
vendor-trace-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trace_bench -Ptb_trace_bench.MEMORY_MODE=$$mode -o build/vendor_trace_bench_$$mode tb/tb_trace_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_trace_bench_$$mode || exit 1; done

test: test-trace-bit test-trace-system test-trace-faults

.PHONY: verify-cp17
verify-cp17:
	$(PYTHON) tools/verify_cp17.py

.PHONY: test-system-flags vendor-system-flags
build/system_flags_vectors: tb/system_flags_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DORACLE_IRQ_PROFILE=1 -DORACLE_TRACE_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/system_flags_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@
build/system_flags-vectors.txt: build/system_flags_vectors
	$< > $@.tmp 2> build/system_flags-oracle.log
	mv $@.tmp $@
test-system-flags: microcode-m0 build/system_flags-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_trace_bit -Ptb_trace_bit.SYSTEM_FLAGS=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/system_flags_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/system_flags_$$mode || exit 1; done
vendor-system-flags: microcode-m0 build/system_flags-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trace_bit -Ptb_trace_bit.SYSTEM_FLAGS=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/vendor_system_flags_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_system_flags_$$mode || exit 1; done

.PHONY: benchmark-system-flags vendor-system-flags-benchmark
benchmark-system-flags: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_system_flags_bench -Ptb_system_flags_bench.MEMORY_MODE=$$mode -o build/system_flags_bench_$$mode tb/tb_system_flags_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/system_flags_bench_$$mode || exit 1; done
vendor-system-flags-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_system_flags_bench -Ptb_system_flags_bench.MEMORY_MODE=$$mode -o build/vendor_system_flags_bench_$$mode tb/tb_system_flags_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_system_flags_bench_$$mode || exit 1; done

.PHONY: verify-cp18
verify-cp18:
	$(PYTHON) tools/verify_cp18.py

test: test-system-flags

.PHONY: test-psw-transfer vendor-psw-transfer
build/psw_transfer_vectors: tb/psw_transfer_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DORACLE_IRQ_PROFILE=1 -DORACLE_TRACE_PROFILE=1 -DORACLE_PSW_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/psw_transfer_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@
build/psw_transfer-vectors.txt: build/psw_transfer_vectors
	$< > $@.tmp 2> build/psw_transfer-oracle.log
	mv $@.tmp $@
test-psw-transfer: microcode-m0 build/psw_transfer-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_trace_bit -Ptb_trace_bit.PSW_TRANSFER=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/psw_transfer_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/psw_transfer_$$mode || exit 1; done
vendor-psw-transfer: microcode-m0 build/psw_transfer-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trace_bit -Ptb_trace_bit.PSW_TRANSFER=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/vendor_psw_transfer_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_psw_transfer_$$mode || exit 1; done


.PHONY: test-psw-transfer-fault vendor-psw-transfer-fault
build/psw_transfer_fault_vectors: tb/psw_transfer_fault_vectors.c tb/bus_fault_vectors.c build/oracle_fault_core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. -I../core tb/psw_transfer_fault_vectors.c build/oracle_fault_core.c ../core/hardware.c -lm -o $@
build/psw-transfer-fault-vectors.txt: build/psw_transfer_fault_vectors
	$< > $@.tmp 2> build/psw-transfer-fault-oracle.log
	mv $@.tmp $@
test-psw-transfer-fault: microcode-m0 build/psw-transfer-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_bus_fault -Ptb_bus_fault.PSW_TRANSFER=1 -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/psw_transfer_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/psw_transfer_fault_$$mode || exit 1; done
vendor-psw-transfer-fault: microcode-m0 build/psw-transfer-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_bus_fault -Ptb_bus_fault.PSW_TRANSFER=1 -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/vendor_psw_transfer_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_psw_transfer_fault_$$mode || exit 1; done

test: test-psw-transfer test-psw-transfer-fault

.PHONY: benchmark-psw-transfer vendor-psw-transfer-benchmark
benchmark-psw-transfer: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_psw_transfer_bench -Ptb_psw_transfer_bench.MEMORY_MODE=$$mode -o build/psw_transfer_bench_$$mode tb/tb_psw_transfer_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/psw_transfer_bench_$$mode || exit 1; done
vendor-psw-transfer-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_psw_transfer_bench -Ptb_psw_transfer_bench.MEMORY_MODE=$$mode -o build/vendor_psw_transfer_bench_$$mode tb/tb_psw_transfer_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_psw_transfer_bench_$$mode || exit 1; done


.PHONY: verify-cp19
verify-cp19:
	$(PYTHON) tools/verify_cp19.py

.PHONY: test-system-control vendor-system-control
build/system_control_vectors: tb/system_control_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DORACLE_IRQ_PROFILE=1 -DORACLE_TRACE_PROFILE=1 -DORACLE_PSW_PROFILE=1 -DORACLE_RESET_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/system_control_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@
build/system_control-vectors.txt: build/system_control_vectors
	$< > $@.tmp 2> build/system_control-oracle.log
	mv $@.tmp $@
test-system-control: microcode-m0 build/system_control-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_trace_bit -Ptb_trace_bit.SYSTEM_CONTROL=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/system_control_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/system_control_$$mode || exit 1; done
vendor-system-control: microcode-m0 build/system_control-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trace_bit -Ptb_trace_bit.SYSTEM_CONTROL=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/vendor_system_control_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_system_control_$$mode || exit 1; done



test: test-system-control


test-system-control-lsi11: microcode-m0 | build
	$(IVERILOG) -g2012 -Wall -s tb_system_control_lsi11 -o build/system_control_lsi11 tb/tb_system_control_lsi11.v $(CORE) $(FRAM) $(FRAM_MODEL) $(IRQ_LEGACY) rtl/uj11_irq_lsi11.v rtl/uj11_rom.v
	$(VVP) build/system_control_lsi11

vendor-system-control-lsi11: microcode-m0 | build
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_system_control_lsi11 -o build/vendor_system_control_lsi11 tb/tb_system_control_lsi11.v $(CORE) $(FRAM) $(FRAM_MODEL) $(IRQ_LEGACY) rtl/uj11_irq_lsi11.v $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_system_control_lsi11

test-system-control-faults: microcode-m0 | build
	$(IVERILOG) -g2012 -Wall -s tb_system_control_faults -o build/system_control_faults tb/tb_system_control_faults.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/system_control_faults

vendor-system-control-faults: microcode-m0 | build
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_system_control_faults -o build/vendor_system_control_faults tb/tb_system_control_faults.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_system_control_faults

test: test-system-control-lsi11 test-system-control-faults


benchmark-system-control: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_system_control_bench -Ptb_system_control_bench.MEMORY_MODE=$$mode -o build/system_control_bench_$$mode tb/tb_system_control_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/system_control_bench_$$mode || exit 1; done

vendor-system-control-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_system_control_bench -Ptb_system_control_bench.MEMORY_MODE=$$mode -o build/vendor_system_control_bench_$$mode tb/tb_system_control_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_system_control_bench_$$mode || exit 1; done

.PHONY: verify-cp20
verify-cp20:
	$(PYTHON) tools/verify_cp20.py

.PHONY: test-eis-ash vendor-eis-ash
build/eis_ash_vectors: tb/eis_ash_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DORACLE_IRQ_PROFILE=1 -DORACLE_TRACE_PROFILE=1 -DORACLE_PSW_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/eis_ash_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@
build/eis_ash-vectors.txt: build/eis_ash_vectors
	$< > $@.tmp 2> build/eis_ash-oracle.log
	mv $@.tmp $@
test-eis-ash: microcode-m0 build/eis_ash-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_trace_bit -Ptb_trace_bit.EIS_ASH=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/eis_ash_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_ash_$$mode || exit 1; done
vendor-eis-ash: microcode-m0 build/eis_ash-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trace_bit -Ptb_trace_bit.EIS_ASH=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/vendor_eis_ash_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_ash_$$mode || exit 1; done



test: test-eis-ash

.PHONY: test-eis-ash-fault vendor-eis-ash-fault
build/eis_ash_fault_vectors: tb/eis_ash_fault_vectors.c tb/bus_fault_vectors.c build/oracle_fault_core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. -I../core tb/eis_ash_fault_vectors.c build/oracle_fault_core.c ../core/hardware.c -lm -o $@
build/eis-ash-fault-vectors.txt: build/eis_ash_fault_vectors
	$< > $@.tmp 2> build/eis-ash-fault-oracle.log
	mv $@.tmp $@
test-eis-ash-fault: microcode-m0 build/eis-ash-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_bus_fault -Ptb_bus_fault.EIS_ASH=1 -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/eis_ash_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_ash_fault_$$mode || exit 1; done
vendor-eis-ash-fault: microcode-m0 build/eis-ash-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_bus_fault -Ptb_bus_fault.EIS_ASH=1 -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/vendor_eis_ash_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_ash_fault_$$mode || exit 1; done

test: test-eis-ash test-eis-ash-fault


.PHONY: benchmark-eis-ash vendor-eis-ash-benchmark
benchmark-eis-ash: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_eis_ash_bench -Ptb_eis_ash_bench.MEMORY_MODE=$$mode -o build/eis_ash_bench_$$mode tb/tb_eis_ash_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_ash_bench_$$mode || exit 1; done
vendor-eis-ash-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_eis_ash_bench -Ptb_eis_ash_bench.MEMORY_MODE=$$mode -o build/vendor_eis_ash_bench_$$mode tb/tb_eis_ash_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_ash_bench_$$mode || exit 1; done



.PHONY: verify-cp21
verify-cp21:
	$(PYTHON) tools/verify_cp21.py

.PHONY: test-eis-ashc vendor-eis-ashc
build/eis_ashc_vectors: tb/eis_ashc_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DORACLE_IRQ_PROFILE=1 -DORACLE_TRACE_PROFILE=1 -DORACLE_PSW_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/eis_ashc_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@
build/eis_ashc-vectors.txt: build/eis_ashc_vectors
	$< > $@.tmp 2> build/eis_ashc-oracle.log
	mv $@.tmp $@
test-eis-ashc: microcode-m0 build/eis_ashc-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_trace_bit -Ptb_trace_bit.EIS_ASHC=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/eis_ashc_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_ashc_$$mode || exit 1; done
vendor-eis-ashc: microcode-m0 build/eis_ashc-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trace_bit -Ptb_trace_bit.EIS_ASHC=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/vendor_eis_ashc_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_ashc_$$mode || exit 1; done



test: test-eis-ashc

.PHONY: test-eis-ashc-fault vendor-eis-ashc-fault
build/eis_ashc_fault_vectors: tb/eis_ashc_fault_vectors.c tb/bus_fault_vectors.c build/oracle_fault_core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. -I../core tb/eis_ashc_fault_vectors.c build/oracle_fault_core.c ../core/hardware.c -lm -o $@
build/eis-ashc-fault-vectors.txt: build/eis_ashc_fault_vectors
	$< > $@.tmp 2> build/eis-ashc-fault-oracle.log
	mv $@.tmp $@
test-eis-ashc-fault: microcode-m0 build/eis-ashc-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_bus_fault -Ptb_bus_fault.EIS_ASHC=1 -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/eis_ashc_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_ashc_fault_$$mode || exit 1; done
vendor-eis-ashc-fault: microcode-m0 build/eis-ashc-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_bus_fault -Ptb_bus_fault.EIS_ASHC=1 -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/vendor_eis_ashc_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_ashc_fault_$$mode || exit 1; done

test: test-eis-ashc test-eis-ashc-fault


.PHONY: benchmark-eis-ashc vendor-eis-ashc-benchmark
benchmark-eis-ashc: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_eis_ashc_bench -Ptb_eis_ashc_bench.MEMORY_MODE=$$mode -o build/eis_ashc_bench_$$mode tb/tb_eis_ashc_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_ashc_bench_$$mode || exit 1; done
vendor-eis-ashc-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_eis_ashc_bench -Ptb_eis_ashc_bench.MEMORY_MODE=$$mode -o build/vendor_eis_ashc_bench_$$mode tb/tb_eis_ashc_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_ashc_bench_$$mode || exit 1; done




.PHONY: verify-cp22
verify-cp22:
	$(PYTHON) tools/verify_cp22.py

build/eis_ashc_alias_vectors: tb/eis_ashc_alias_vectors.c tb/eis_ashc_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DORACLE_IRQ_PROFILE=1 -DORACLE_TRACE_PROFILE=1 -DORACLE_PSW_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/eis_ashc_alias_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@
build/eis_ashc_alias-vectors.txt: build/eis_ashc_alias_vectors
	$< > $@.tmp 2> build/eis_ashc_alias-oracle.log
	mv $@.tmp $@

.PHONY: test-ashc-alias
test-ashc-alias:
	$(PYTHON) tools/check_ashc_alias.py

.PHONY: test-reference-core
test-reference-core: | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. ../tests/core_tests.c ../core/core.c ../core/disas.c ../core/hardware.c -lm -o build/cp22-core-tests
	build/cp22-core-tests

.PHONY: verify-cp23 test-seq-equivalence
YOSYS ?= yosys
verify-cp23:
	YOSYS="$(YOSYS)" $(PYTHON) tools/verify_cp23.py
test-seq-equivalence: | build
	$(PYTHON) tools/check_seq_cp23.py --yosys "$(YOSYS)"

.PHONY: test-eis-xor vendor-eis-xor
build/eis_xor_vectors: tb/eis_xor_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DORACLE_IRQ_PROFILE=1 -DORACLE_TRACE_PROFILE=1 -DORACLE_PSW_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/eis_xor_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@
build/eis_xor-vectors.txt: build/eis_xor_vectors
	$< > $@.tmp 2> build/eis_xor-oracle.log
	mv $@.tmp $@
test-eis-xor: microcode-m0 build/eis_xor-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_trace_bit -Ptb_trace_bit.EIS_XOR=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/eis_xor_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_xor_$$mode || exit 1; done
vendor-eis-xor: microcode-m0 build/eis_xor-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trace_bit -Ptb_trace_bit.EIS_XOR=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/vendor_eis_xor_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_xor_$$mode || exit 1; done



test: test-eis-xor

.PHONY: test-eis-xor-fault vendor-eis-xor-fault
build/eis_xor_fault_vectors: tb/eis_xor_fault_vectors.c tb/bus_fault_vectors.c build/oracle_fault_core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. -I../core tb/eis_xor_fault_vectors.c build/oracle_fault_core.c ../core/hardware.c -lm -o $@
build/eis-xor-fault-vectors.txt: build/eis_xor_fault_vectors
	$< > $@.tmp 2> build/eis-xor-fault-oracle.log
	mv $@.tmp $@
test-eis-xor-fault: microcode-m0 build/eis-xor-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_bus_fault -Ptb_bus_fault.EIS_XOR=1 -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/eis_xor_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_xor_fault_$$mode || exit 1; done
vendor-eis-xor-fault: microcode-m0 build/eis-xor-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_bus_fault -Ptb_bus_fault.EIS_XOR=1 -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/vendor_eis_xor_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_xor_fault_$$mode || exit 1; done

test: test-eis-xor test-eis-xor-fault



.PHONY: benchmark-eis-xor vendor-eis-xor-benchmark
benchmark-eis-xor: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_eis_xor_bench -Ptb_eis_xor_bench.MEMORY_MODE=$$mode -o build/eis_xor_bench_$$mode tb/tb_eis_xor_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_xor_bench_$$mode || exit 1; done
vendor-eis-xor-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_eis_xor_bench -Ptb_eis_xor_bench.MEMORY_MODE=$$mode -o build/vendor_eis_xor_bench_$$mode tb/tb_eis_xor_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_xor_bench_$$mode || exit 1; done





.PHONY: test-xor-io vendor-xor-io
test-xor-io: microcode-m0
	$(IVERILOG) -g2012 -Wall -s tb_xor_io -o build/xor_io tb/tb_xor_io.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/xor_io
vendor-xor-io: microcode-m0
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_xor_io -o build/vendor_xor_io tb/tb_xor_io.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_xor_io



test: test-xor-io

.PHONY: verify-cp24 test-xor-oracle test-xor-negative
verify-cp24:
	$(PYTHON) tools/verify_cp24.py
test-xor-oracle: build/eis_xor-vectors.txt build/eis-xor-fault-vectors.txt
	$(PYTHON) tools/check_xor_oracle.py
test-xor-negative: build/eis_xor-vectors.txt build/eis-xor-fault-vectors.txt
	$(PYTHON) tools/check_xor_negative.py

.PHONY: test-eis-mul vendor-eis-mul
build/eis_mul_vectors: tb/eis_mul_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DORACLE_IRQ_PROFILE=1 -DORACLE_TRACE_PROFILE=1 -DORACLE_PSW_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/eis_mul_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@
build/eis_mul-vectors.txt: build/eis_mul_vectors
	$< > $@.tmp 2> build/eis_mul-oracle.log
	mv $@.tmp $@
test-eis-mul: microcode-m0 build/eis_mul-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_trace_bit -Ptb_trace_bit.EIS_MUL=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/eis_mul_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_mul_$$mode || exit 1; done
vendor-eis-mul: microcode-m0 build/eis_mul-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trace_bit -Ptb_trace_bit.EIS_MUL=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/vendor_eis_mul_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_mul_$$mode || exit 1; done



test: test-eis-mul

.PHONY: test-eis-mul-fault vendor-eis-mul-fault
build/eis_mul_fault_vectors: tb/eis_mul_fault_vectors.c tb/bus_fault_vectors.c build/oracle_fault_core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. -I../core tb/eis_mul_fault_vectors.c build/oracle_fault_core.c ../core/hardware.c -lm -o $@
build/eis-mul-fault-vectors.txt: build/eis_mul_fault_vectors
	$< > $@.tmp 2> build/eis-mul-fault-oracle.log
	mv $@.tmp $@
test-eis-mul-fault: microcode-m0 build/eis-mul-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_bus_fault -Ptb_bus_fault.EIS_MUL=1 -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/eis_mul_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_mul_fault_$$mode || exit 1; done
vendor-eis-mul-fault: microcode-m0 build/eis-mul-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_bus_fault -Ptb_bus_fault.EIS_MUL=1 -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/vendor_eis_mul_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_mul_fault_$$mode || exit 1; done

test: test-eis-mul test-eis-mul-fault




.PHONY: benchmark-eis-mul vendor-eis-mul-benchmark
benchmark-eis-mul: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_eis_mul_bench -Ptb_eis_mul_bench.MEMORY_MODE=$$mode -o build/eis_mul_bench_$$mode tb/tb_eis_mul_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_mul_bench_$$mode || exit 1; done
vendor-eis-mul-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_eis_mul_bench -Ptb_eis_mul_bench.MEMORY_MODE=$$mode -o build/vendor_eis_mul_bench_$$mode tb/tb_eis_mul_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_mul_bench_$$mode || exit 1; done





.PHONY: test-mul-io vendor-mul-io
test-mul-io: microcode-m0
	$(IVERILOG) -g2012 -Wall -s tb_mul_io -o build/mul_io tb/tb_mul_io.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/mul_io
vendor-mul-io: microcode-m0
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_mul_io -o build/vendor_mul_io tb/tb_mul_io.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_mul_io



test: test-mul-io


.PHONY: verify-cp25 test-mul-oracle test-mul-negative
verify-cp25:
	$(PYTHON) tools/verify_cp25.py
test-mul-oracle: build/eis_mul-vectors.txt build/eis-mul-fault-vectors.txt
	$(PYTHON) tools/check_mul_oracle.py
test-mul-negative: build/eis_mul-vectors.txt build/eis-mul-fault-vectors.txt
	$(PYTHON) tools/check_mul_negative.py

# CP26 DIV
.PHONY: test-eis-div vendor-eis-div
build/eis_div_vectors: tb/eis_div_vectors.c tb/trace_oracle.h build/oracle_control_core.c ../core/core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DORACLE_ACCESS_AUDIT=1 -DORACLE_VECTOR_AUDIT=1 -DORACLE_TRAP_PROFILE=1 -DORACLE_IRQ_PROFILE=1 -DORACLE_TRACE_PROFILE=1 -DORACLE_PSW_PROFILE=1 -DENABLE_MMU=0 -I.. -I../core tb/eis_div_vectors.c build/oracle_control_core.c ../core/hardware.c -lm -o $@
build/eis_div-vectors.txt: build/eis_div_vectors
	$< > $@.tmp 2> build/eis_div-oracle.log
	mv $@.tmp $@
test-eis-div: microcode-m0 build/eis_div-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_trace_bit -Ptb_trace_bit.EIS_DIV=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/eis_div_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_div_$$mode || exit 1; done
vendor-eis-div: microcode-m0 build/eis_div-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_trace_bit -Ptb_trace_bit.EIS_DIV=1 -Ptb_trace_bit.MEMORY_MODE=$$mode -o build/vendor_eis_div_$$mode tb/tb_trace_bit.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_div_$$mode || exit 1; done



test: test-eis-div

.PHONY: test-eis-div-fault vendor-eis-div-fault
build/eis_div_fault_vectors: tb/eis_div_fault_vectors.c tb/bus_fault_vectors.c build/oracle_fault_core.c ../core/core.h ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=0 -I.. -I../core tb/eis_div_fault_vectors.c build/oracle_fault_core.c ../core/hardware.c -lm -o $@
build/eis-div-fault-vectors.txt: build/eis_div_fault_vectors
	$< > $@.tmp 2> build/eis-div-fault-oracle.log
	mv $@.tmp $@
test-eis-div-fault: microcode-m0 build/eis-div-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_bus_fault -Ptb_bus_fault.EIS_DIV=1 -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/eis_div_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_div_fault_$$mode || exit 1; done
vendor-eis-div-fault: microcode-m0 build/eis-div-fault-vectors.txt
	@for mode in $(EA_TEST_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_bus_fault -Ptb_bus_fault.EIS_DIV=1 -Ptb_bus_fault.MEMORY_MODE=$$mode -o build/vendor_eis_div_fault_$$mode tb/tb_bus_fault.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_div_fault_$$mode || exit 1; done

test: test-eis-div test-eis-div-fault




.PHONY: benchmark-eis-div vendor-eis-div-benchmark
benchmark-eis-div: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -Wall -s tb_eis_div_bench -Ptb_eis_div_bench.MEMORY_MODE=$$mode -o build/eis_div_bench_$$mode tb/tb_eis_div_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) rtl/uj11_rom.v && \
	  $(VVP) build/eis_div_bench_$$mode || exit 1; done
vendor-eis-div-benchmark: microcode-m0 | build
	@for mode in $(EA_MODES); do \
	  $(IVERILOG) -g2012 -DVENDOR_ROM -s tb_eis_div_bench -Ptb_eis_div_bench.MEMORY_MODE=$$mode -o build/vendor_eis_div_bench_$$mode tb/tb_eis_div_bench.v tb/uj11_ram.v $(CORE) $(FRAM) $(FRAM_MODEL) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v" && \
	  $(VVP) build/vendor_eis_div_bench_$$mode || exit 1; done





.PHONY: test-div-io vendor-div-io
test-div-io: microcode-m0
	$(IVERILOG) -g2012 -Wall -s tb_div_io -o build/div_io tb/tb_div_io.v $(CORE) rtl/uj11_rom.v
	$(VVP) build/div_io
vendor-div-io: microcode-m0
	$(IVERILOG) -g2012 -DVENDOR_ROM -s tb_div_io -o build/vendor_div_io tb/tb_div_io.v $(CORE) $(GEN)/uj11_m0_ebr.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/vendor_div_io



test: test-div-io


.PHONY: verify-cp26 test-div-oracle test-div-negative
verify-cp26:
	$(PYTHON) tools/verify_cp26.py
test-div-oracle: build/eis_div-vectors.txt build/eis-div-fault-vectors.txt
	$(PYTHON) tools/check_div_oracle.py
test-div-negative: build/eis_div-vectors.txt build/eis-div-fault-vectors.txt
	$(PYTHON) tools/check_div_negative.py

# CP27: FIS uses an independent exact F-format reference, not host C float.
.PHONY: test-fis vendor-fis test-fis-negative benchmark-fis vendor-fis-benchmark test-datapath-equivalence verify-cp27
build/fis-vectors.txt: tools/make_fis_vectors.py tools/fis_reference.py | build
	$(PYTHON) tools/make_fis_vectors.py

test-fis: microcode-m0 build/fis-vectors.txt
	$(PYTHON) tools/run_fis_tests.py

vendor-fis: microcode-m0 build/fis-vectors.txt
	$(PYTHON) tools/run_fis_tests.py --vendor --jobs 4

test-fis-negative: microcode-m0 | build
	$(PYTHON) tools/check_fis_negative.py

benchmark-fis: microcode-m0 | build
	$(PYTHON) tools/benchmark_fis.py

vendor-fis-benchmark: microcode-m0 | build
	$(PYTHON) tools/benchmark_fis.py --vendor

test-datapath-equivalence: microcode-m0 | build
	$(PYTHON) tools/check_datapath_cp27.py --yosys $(YOSYS)

verify-cp27:
	$(PYTHON) tools/verify_cp27.py

test: test-fis

# CP28 full physical HC1200 board, firmware assembled by the existing microasm11.
.PHONY: board board-firmware board-decode test-board-units test-board-isa test-board-rt11 synthesis-board verify-cp28
BOARD_CHECKPOINT ?= cp29a
board: microcode-m0 board-firmware board-decode
board-firmware: | build
	$(PYTHON) tools/build_firmware.py
board-decode: | build
	$(PYTHON) tools/build_decode_rom.py
test-board-units: board
	$(PYTHON) tools/check_board_units.py
test-board-isa: board build/isa_vectors.mem build/ea-vectors.txt build/irq-vectors.txt build/trace_bit-vectors.txt build/bus-fault-vectors.txt build/fis-vectors.txt
	$(PYTHON) tools/check_sync_decode.py
	$(PYTHON) tools/check_sync_decode.py --suite fis
test-board-rt11: board
	$(PYTHON) tools/run_board.py
synthesis-board: board
	$(PYTHON) tools/checkpoint_board.py $(BOARD_CHECKPOINT)
verify-cp28:
	$(PYTHON) tools/verify_cp28.py --yosys $(YOSYS)

# CP29: panel/HG pins and explicitly invoked hardware deployment.
.PHONY: test-board-panel export-board
# Use a fresh BOARD_CHECKPOINT for synthesis; the published CP29a is immutable.
test-board-panel: board
	$(PYTHON) tools/check_panel.py
export-board:
	$(PYTHON) tools/export_board.py $(BOARD_CHECKPOINT)

.PHONY: test-board-selectors
test-board-selectors: board
	PYTHONPATH=tools $(PYTHON) -c "from check_board_units import run; from board_common import BOARD; run('tb_board_bus','cp30-board-selector',BOARD+['reference/lsi11/spi_fram_model.v'],['-Ptb_board_bus.CHECK_SELECTORS=1'])"

# CP31: isolated 18-bit translation/permission checkpoint, not in board sources.
.PHONY: test-mmu18
test-mmu18: | build
	$(IVERILOG) -g2012 -Wall -s tb_mmu18 -o build/mmu18 tb/tb_mmu18.v rtl/uj11_mmu_translate18.v
	$(VVP) build/mmu18
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_mmu18 synth/machxo2/uj11_probe_mmu18.v rtl/uj11_mmu_translate18.v

.PHONY: test-mmu-translate
test-mmu-translate: | build
	$(IVERILOG) -g2012 -Wall -s tb_mmu_translate -Ptb_mmu_translate.FULL=0 -o build/mmu_translate tb/tb_mmu_translate.v rtl/uj11_mmu_translate.v rtl/uj11_mmu_translate18.v
	$(VVP) build/mmu_translate
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_mmu synth/machxo2/uj11_probe_mmu.v rtl/uj11_mmu_translate.v
	$(VERILATOR) --binary --timing -j 4 --top-module tb_mmu_translate --Mdir build/obj-mmu-translate tb/tb_mmu_translate.v rtl/uj11_mmu_translate.v rtl/uj11_mmu_translate18.v
	build/obj-mmu-translate/Vtb_mmu_translate

.PHONY: test-mmu-oracle
build/reference_mmu_translate: tb/reference_mmu_translate.c ../core/core.c ../core/core.h ../core/pdp11_fp.c ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=1 -I.. tb/reference_mmu_translate.c ../core/hardware.c -lm -o $@
build/mmu-oracle.txt: build/reference_mmu_translate
	$< > $@.tmp
	mv $@.tmp $@
test-mmu-oracle: build/mmu-oracle.txt
	$(IVERILOG) -g2012 -Wall -s tb_mmu_oracle -o build/mmu_oracle tb/tb_mmu_oracle.v rtl/uj11_mmu_translate.v
	$(VVP) build/mmu_oracle

.PHONY: test-mmu-fram
test-mmu-fram: | build
	$(PYTHON) tools/run_mmu_fram.py

.PHONY: test-mmu-apr
test-mmu-apr: | build
	$(IVERILOG) -g2012 -Wall -s tb_mmu_apr -o build/mmu_apr tb/tb_mmu_apr.v rtl/uj11_mmu_apr.v rtl/uj11_mmu_apr_ram.v
	$(VVP) build/mmu_apr
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_apr synth/machxo2/uj11_probe_apr.v rtl/uj11_mmu_apr.v rtl/uj11_mmu_apr_ram.v

.PHONY: test-mmu-apr-decode
test-mmu-apr-decode: | build
	$(IVERILOG) -g2012 -Wall -s tb_mmu_apr_decode -o build/mmu_apr_decode tb/tb_mmu_apr_decode.v rtl/uj11_mmu_apr_decode.v
	$(VVP) build/mmu_apr_decode
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_apr_csr synth/machxo2/uj11_probe_apr_csr.v rtl/uj11_mmu_apr.v rtl/uj11_mmu_apr_ram.v rtl/uj11_mmu_apr_decode.v

build/reference_mmu_apr: tb/reference_mmu_apr.c ../core/core.c ../core/core.h ../core/pdp11_fp.c ../core/hardware.c ../core/hardware.h | build
	$(CC) -O2 -Wall -DENABLE_MMU=1 -I.. tb/reference_mmu_apr.c ../core/hardware.c -lm -o $@
build/mmu-apr-oracle.txt: build/reference_mmu_apr
	$< > $@.tmp
	mv $@.tmp $@
.PHONY: test-mmu-apr-oracle
test-mmu-apr-oracle: build/mmu-apr-oracle.txt
	$(IVERILOG) -g2012 -Wall -s tb_mmu_apr_oracle -o build/mmu_apr_oracle tb/tb_mmu_apr_oracle.v rtl/uj11_mmu_apr.v rtl/uj11_mmu_apr_ram.v rtl/uj11_mmu_apr_decode.v rtl/uj11_mmu_translate.v
	$(VVP) build/mmu_apr_oracle

.PHONY: vendor-mmu-apr
vendor-mmu-apr: build/mmu-apr-oracle.txt
	$(IVERILOG) -g2012 -DUJ11_VENDOR_ROM -s tb_mmu_apr -o build/mmu_apr_vendor tb/tb_mmu_apr.v rtl/uj11_mmu_apr.v rtl/uj11_mmu_apr_ram.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/mmu_apr_vendor
	$(IVERILOG) -g2012 -DUJ11_VENDOR_ROM -s tb_mmu_apr_oracle -o build/mmu_apr_oracle_vendor tb/tb_mmu_apr_oracle.v rtl/uj11_mmu_apr.v rtl/uj11_mmu_apr_ram.v rtl/uj11_mmu_apr_decode.v rtl/uj11_mmu_translate.v "$(LATTICE_SIM_DIR)/DP8KC.v" "$(LATTICE_SIM_DIR)/GSR.v" "$(LATTICE_SIM_DIR)/PUR.v"
	$(VVP) build/mmu_apr_oracle_vendor

MMU_DP_COMPARE = rtl/uj11_alu.v rtl/uj11_regfile.v rtl/uj11_datapath.v rtl/experimental/uj11_datapath_borrow.v synth/machxo2/uj11_mmu_dp_compare.v
MMU_DP_CANDIDATE ?= 1
.PHONY: test-mmu-dp-sharing
test-mmu-dp-sharing: | build
	$(IVERILOG) -g2012 -Wall -s tb_mmu_dp_compare -Ptb_mmu_dp_compare.FULL=0 -Ptb_mmu_dp_compare.CANDIDATE=$(MMU_DP_CANDIDATE) -o build/mmu_dp_compare tb/tb_mmu_dp_compare.v $(MMU_DP_COMPARE)
	$(VVP) build/mmu_dp_compare
	$(VERILATOR) --lint-only --Wall --top-module uj11_probe_mmu_dp_shared synth/machxo2/uj11_probe_mmu_dp_shared.v synth/machxo2/uj11_probe_mmu_dp.v $(MMU_DP_COMPARE)
	$(VERILATOR) --binary --timing -j 4 --top-module tb_mmu_dp_compare -GCANDIDATE=$(MMU_DP_CANDIDATE) --Mdir build/obj-mmu-dp-compare tb/tb_mmu_dp_compare.v $(MMU_DP_COMPARE)
	build/obj-mmu-dp-compare/Vtb_mmu_dp_compare

.PHONY: test-mmu-dp-negative test-mmu-scratch
test-mmu-dp-negative: | build
	$(PYTHON) tools/check_mmu_dp_negative.py

test-mmu-scratch: microcode-m0 build/isa_vectors.mem build/ea-vectors.txt build/irq-vectors.txt build/trace_bit-vectors.txt build/bus-fault-vectors.txt build/fis-vectors.txt build/eis_ash-vectors.txt build/eis-ash-fault-vectors.txt build/eis_ashc-vectors.txt build/eis-ashc-fault-vectors.txt build/eis_xor-vectors.txt build/eis-xor-fault-vectors.txt build/eis_mul-vectors.txt build/eis-mul-fault-vectors.txt build/eis_div-vectors.txt build/eis-div-fault-vectors.txt build/single-vectors.txt build/control-vectors.txt build/extra-vectors.txt build/psw_transfer-vectors.txt build/system_control-vectors.txt
	$(PYTHON) tools/check_mmu_scratch.py
	$(PYTHON) tools/check_mmu_scratch_coverage.py

# CP35: experimental build-only memory context hook; production stays CP31c.
.PHONY: mmu-entry test-mmu-entry test-mmu-entry-fis test-mmu-entry-board
mmu-entry: board | build
	$(PYTHON) tools/build_mmu_entry.py
test-mmu-entry: mmu-entry
	$(IVERILOG) -g2012 -Wall -s tb_mmu_entry -o build/cp35-entry tb/tb_mmu_entry.v rtl/experimental/uj11_mmu_entry.v
	$(VVP) build/cp35-entry > build/cp35-entry.log
	$(PYTHON) tools/check_mmu_entry_lint.py
	$(PYTHON) tools/run_mmu_entry.py
	$(PYTHON) tools/run_mmu_entry.py --simulator iverilog --cases 1024
	$(PYTHON) tools/check_mmu_entry_negative.py
test-mmu-entry-fis: mmu-entry build/fis-vectors.txt
	$(PYTHON) tools/check_mmu_entry_fis.py
test-mmu-entry-board: mmu-entry
	$(PYTHON) tools/run_mmu_entry_board.py

# CP36: compact opcode index and aligned-word board interface.
.PHONY: decode-compact decode-word-bus test-decode-compact test-decode-cpu test-decode-board
decode-compact: board
	$(PYTHON) tools/build_decode_compact.py
decode-word-bus: mmu-entry
	$(PYTHON) tools/build_decode_word_bus.py
test-decode-compact: decode-compact
	$(PYTHON) tools/check_decode_compact.py
test-decode-cpu: decode-compact decode-word-bus
	$(PYTHON) tools/check_decode_lint.py
	$(PYTHON) tools/check_decode_cpu.py
	$(PYTHON) tools/check_decode_cpu.py --simulator iverilog --cases 1024
	$(PYTHON) tools/check_decode_cpu.py --simulator iverilog --cases 1024 --negative-lanes
test-decode-board: board
	$(PYTHON) tools/run_board.py --tag cp36

# CP37d remains experimental: read-only APR lookup, no CPU CSR/translation.
.PHONY: mmu-apr-lookup test-mmu-apr-lookup test-mmu-apr-lookup-fis test-mmu-apr-lookup-board
mmu-apr-lookup: board
	$(PYTHON) tools/build_mmu_apr_lookup.py --page --scheduled --masked-d
test-mmu-apr-lookup: mmu-apr-lookup
	$(PYTHON) tools/check_mmu_apr_lookup_lint.py
	$(PYTHON) tools/run_mmu_apr_lookup.py
	$(PYTHON) tools/run_mmu_apr_lookup.py --simulator iverilog --cases 1024
	$(PYTHON) tools/run_mmu_apr_lookup.py --simulator iverilog --cases 1024 --no-holds
	$(PYTHON) tools/check_mmu_apr_lookup_negative.py
test-mmu-apr-lookup-fis: mmu-apr-lookup build/fis-vectors.txt
	$(PYTHON) tools/check_mmu_apr_lookup_fis.py
test-mmu-apr-lookup-board: mmu-apr-lookup
	$(PYTHON) tools/run_mmu_apr_lookup_board.py
