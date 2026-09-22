PYTHON ?= python3
IVERILOG ?= iverilog
VVP ?= vvp
OUT ?= build/hc1200

# Runners share generated ROM/module inputs and validate their hashes.
.NOTPARALLEL:

.PHONY: all hardware software test test-python test-rtl test-modules test-fpp test-fpp-full test-odt test-rt11 test-basic test-configuration verify synthesis prepare-synthesis export clean
all: hardware software
hardware:
	$(PYTHON) tools/build_hardware.py
software:
	$(PYTHON) tools/build_software.py
	$(PYTHON) tools/build_fpp.py
verify: all
	$(PYTHON) tools/verify_production.py

test: verify test-python test-rtl test-modules test-fpp test-odt
test-python:
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -q
	$(PYTHON) tools/test_module_image.py
	$(PYTHON) tools/test_hardware_uart.py
test-rtl:
	mkdir -p build/unit
	$(IVERILOG) -g2012 -s tb_alu -o build/unit/alu tests/tb_alu.v rtl/uj11_alu.v
	$(VVP) build/unit/alu
	$(IVERILOG) -g2012 -s tb_datapath -o build/unit/datapath tests/tb_datapath.v rtl/uj11_alu.v rtl/uj11_regfile.v rtl/uj11_datapath.v
	$(VVP) build/unit/datapath
	$(IVERILOG) -g2012 -s tb_microseq -o build/unit/seq tests/tb_microseq.v rtl/uj11_microseq.v
	$(VVP) build/unit/seq
	$(IVERILOG) -g2012 -s tb_alu_byte -o build/unit/alu_byte tests/tb_alu_byte.v rtl/uj11_alu.v
	$(VVP) build/unit/alu_byte
	$(IVERILOG) -g2012 -s tb_datapath_byte -o build/unit/datapath_byte tests/tb_datapath_byte.v rtl/uj11_alu.v rtl/uj11_regfile.v rtl/uj11_datapath.v
	$(VVP) build/unit/datapath_byte
	$(IVERILOG) -g2012 -s tb_mem_psw -o build/unit/mem_psw tests/tb_mem_psw.v rtl/uj11_psw.v rtl/uj11_mem.v tests/uj11_ram.v
	$(VVP) build/unit/mem_psw
	$(IVERILOG) -g2012 -s tb_button -o build/unit/button tests/tb_button.v boards/hc1200/uj11_button.v boards/hc1200/uj11_tick.v
	$(VVP) build/unit/button
test-modules: all
	$(PYTHON) tools/test_modules.py
	$(PYTHON) tools/test_loader.py
test-fpp: all
	$(PYTHON) tools/test_fpp_math.py
	$(PYTHON) tools/test_fpp_conversions.py
	$(PYTHON) tools/test_fpp.py --smoke
	$(PYTHON) tools/test_fpp_psw.py --mode sync
	$(PYTHON) tools/test_fpp_events.py
test-fpp-full: all
	$(PYTHON) tools/test_fpp.py --mode sync
	$(PYTHON) tools/test_fpp.py --mode logic
	$(PYTHON) tools/test_fpp_psw.py --mode sync
	$(PYTHON) tools/test_fpp_psw.py --mode logic
	$(PYTHON) tools/test_fpp_events.py
test-odt: all
	$(PYTHON) tools/test_odt.py --odt build/software/odt --out build/test-odt
	$(PYTHON) tools/test_breakpoints.py --odt build/software/odt --out build/test-breakpoints
	$(PYTHON) tools/test_panel.py --odt build/software/odt --out build/test-panel
	$(PYTHON) tools/test_odt_fpp.py --out build/test-odt-fpp
test-rt11:
	$(PYTHON) tools/run_rt11.py --out $(OUT)
test-basic:
	$(PYTHON) tools/run_basic.py --out $(OUT)
test-configuration:
	$(PYTHON) tools/run_basic.py --configuration --out $(OUT)
prepare-synthesis:
	$(PYTHON) tools/synthesis.py $(notdir $(OUT)) --prepare-only
synthesis:
	$(PYTHON) tools/synthesis.py $(notdir $(OUT))
export:
	$(PYTHON) tools/export_jed.py $(notdir $(OUT))
clean:
	$(PYTHON) -c "import shutil; shutil.rmtree('build', ignore_errors=True)"
