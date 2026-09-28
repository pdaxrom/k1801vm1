PYTHON ?= python3
IVERILOG ?= iverilog
VVP ?= vvp
BOARD ?= hc1200
CPU ?= mmuless
FPP ?= microcode
IOP ?= legacy
OUT ?= build/$(BOARD)$(if $(filter mmu,$(CPU)),-mmu$(if $(filter off,$(FPP)),-nofpp)$(if $(filter storage,$(IOP)),-storage))
HG_OUT ?= build/$(if $(filter mmu,$(CPU)),hgx,hg)
HG_TEST_OUT ?= build/$(if $(filter mmu,$(CPU)),test-hgx,test-hg)
SD_OUT ?= build/$(if $(filter mmu,$(CPU)),sd-hc7000,sd)
SD_TEST_OUT ?= build/$(if $(filter mmu,$(CPU)),test-sd-hc7000,test-sd)
BSD_OUT ?= build/sd-bsd
MMU_BOOT_MENU ?= 0
MMU_SD_IMAGE ?= $(if $(filter storage,$(IOP)),build/storage-card.img,../lsi11/disks/rt11v5.3/system.dsk)

ifeq ($(filter $(CPU),mmuless mmu),)
$(error Unknown CPU=$(CPU); expected mmuless or mmu)
endif
ifeq ($(CPU),mmu)
ifeq ($(filter $(FPP),microcode off),)
$(error Unknown FPP=$(FPP); expected microcode or off)
endif
export UJ11_MMU_FPP := $(FPP)
ifeq ($(filter $(IOP),legacy storage),)
$(error Unknown IOP=$(IOP); expected legacy or storage)
endif
export UJ11_MMU_IOP := $(IOP)
ifeq ($(IOP),storage)
ifneq ($(FPP),off)
$(error IOP=storage requires FPP=off)
endif
endif
ifeq ($(FPP),off)
ifneq ($(filter test-mmu-fpp test-sd-image,$(MAKECMDGOALS)),)
$(error This target exercises FP11 programs and requires FPP=microcode; use test or test-rt11 for FPP=off)
endif
endif
ifneq ($(BOARD),hc7000-lcd-sram)
$(error CPU=mmu requires BOARD=hc7000-lcd-sram)
endif
# Explicit inventory: never route MMU requests into a legacy HALT runner.
# Export validates source hashes and the 24 MHz MAP/PAR/TRACE gate.
ifneq ($(filter-out all hardware hg test-hg-time sd-image test-sd-image test test-mmu test-mmu-fpp test-mmu-board test-storage test-storage-controllers bsd-image test-bsd storage-menu test-storage-menu test-python test-rt11 prepare-synthesis synthesis export clean,$(MAKECMDGOALS)),)
$(error Target is unavailable for experimental CPU=mmu; HALT software belongs to CPU=mmuless)
endif
else
ifneq ($(IOP),legacy)
$(error IOP selection is available only for CPU=mmu)
endif
ifneq ($(FPP),microcode)
$(error FPP selection is available only for CPU=mmu)
endif
endif

# Runners share generated ROM/module inputs and validate their hashes.
.NOTPARALLEL:

.PHONY: all hardware software hg test-hg-time sd-image test-sd-image test test-python test-rtl test-modules test-fpp test-fpp-full test-odt test-rt11 test-basic test-configuration test-reboot verify synthesis prepare-synthesis export clean
ifeq ($(CPU),mmu)
all: hardware
else
all: hardware software
endif
hardware:
ifeq ($(CPU),mmu)
	$(PYTHON) tools/build_mmu_board.py
else
	$(if $(filter hc7000-lcd-sram,$(BOARD)),$(PYTHON) tools/build_hc7000.py,$(PYTHON) tools/build_hardware.py)
endif
software: hardware
	$(PYTHON) tools/build_software.py
	$(PYTHON) tools/build_fpp.py
	PYTHONPATH=tools $(PYTHON) -c "from build_software import reboot; reboot()"
hg:
	$(MAKE) -C ../lsi11-fpga/host/hg all
ifeq ($(CPU),mmu)
	$(PYTHON) tools/build_hgx.py --out $(HG_OUT)
else
	$(PYTHON) tools/build_hg.py --out $(HG_OUT)
endif
test-hg-time:
	$(MAKE) -C ../lsi11-fpga/host/hg test
ifeq ($(CPU),mmu)
	$(PYTHON) tools/test_hgx.py --out $(HG_TEST_OUT)
else
	$(PYTHON) tools/test_hg_time.py --out $(HG_TEST_OUT)
endif
sd-image:
ifeq ($(CPU),mmu)
	$(PYTHON) tools/build_sd_mmu.py --out $(SD_OUT)
else
	$(PYTHON) tools/build_sd_image.py --out $(SD_OUT)
endif
test-sd-image:
ifeq ($(CPU),mmu)
	$(PYTHON) tools/test_sd_mmu.py --image $(SD_OUT)/uj11-hc7000-rt11xm.img --out $(SD_TEST_OUT) --rtl
else
	$(PYTHON) tools/test_sd_image.py --image $(SD_OUT)/uj11-rt11fb.img --out $(SD_TEST_OUT) --rtl
endif
verify: all
	$(PYTHON) tools/verify_production.py

ifeq ($(CPU),mmu)
ifeq ($(FPP),off)
test: test-mmu test-mmu-board
else
test: test-mmu test-mmu-fpp test-mmu-board
endif
else
test: verify test-python test-rtl test-modules test-fpp test-odt
endif
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
	$(IVERILOG) -g2012 -s tb_tick -o build/unit/tick tests/tb_tick.v boards/hc1200/uj11_tick.v
	$(VVP) build/unit/tick
test-modules: all
	$(PYTHON) tools/test_modules.py
	$(PYTHON) tools/test_loader.py
	$(PYTHON) tools/test_reboot.py
test-reboot: all
	$(PYTHON) tools/test_reboot.py
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
ifeq ($(CPU),mmu)
	$(PYTHON) tools/test_mmu_board.py --name tb_mmu_boot --monitor xm --image $(MMU_SD_IMAGE) --out $(OUT) $(if $(filter 1,$(MMU_BOOT_MENU)),--boot-menu)
else
	$(PYTHON) tools/run_rt11.py --out $(OUT) --board $(BOARD)
endif
test-basic:
	@test "$(BOARD)" = hc1200 || { echo 'Use the HC7000 hardware BASIC tests; this simulation target is HC1200-only.'; exit 1; }
	$(PYTHON) tools/run_basic.py --out $(OUT)
test-configuration:
	@test "$(BOARD)" = hc1200 || { echo 'The retained FRAM configuration simulation is HC1200-only.'; exit 1; }
	$(PYTHON) tools/run_basic.py --configuration --out $(OUT)
prepare-synthesis:
ifeq ($(CPU),mmu)
	$(PYTHON) tools/synthesis_mmu.py $(notdir $(OUT)) --prepare-only
else
	$(PYTHON) tools/synthesis.py $(notdir $(OUT)) --board $(BOARD) --prepare-only
endif
synthesis:
ifeq ($(CPU),mmu)
	$(PYTHON) tools/synthesis_mmu.py $(notdir $(OUT))
else
	$(PYTHON) tools/synthesis.py $(notdir $(OUT)) --board $(BOARD)
endif
export:
	$(PYTHON) tools/export_jed.py $(notdir $(OUT))
clean:
ifeq ($(CPU),mmu)
	$(PYTHON) -c "import shutil; [shutil.rmtree('build/'+p, ignore_errors=True) for p in ('hc7000-mmu-hardware','hc7000-mmu-iop','test-mmu','test-mmu-bus','test-mmu-disk')]"
else
	$(PYTHON) -c "import shutil; shutil.rmtree('build', ignore_errors=True)"
endif

.PHONY: test-sram test-hc7000-hg
test-sram:
	mkdir -p build/unit
	$(IVERILOG) -g2012 -s tb_sram -o build/unit/sram tests/tb_sram.v boards/hc7000/uj11_sram.v tests/models/async_sram_model.v
	$(VVP) build/unit/sram
test-hc7000-hg:
	mkdir -p build/unit
	$(IVERILOG) -g2012 -s tb_hc7000_hg -o build/unit/hc7000-hg tests/tb_hc7000_hg.v boards/hc7000/uj11_hg_inputs.v
	$(VVP) build/unit/hc7000-hg

.PHONY: iop test-hc7000-disk
iop:
	$(PYTHON) tools/build_iop.py
test-hc7000-disk:
	$(PYTHON) tools/test_hc7000_disk.py --memory --icarus --out build/test-hc7000-iop-ram
	$(PYTHON) tools/test_hc7000_disk.py

.PHONY: test-mmu
test-mmu:
	$(PYTHON) -m unittest discover -s tests -p 'test_mmu_*.py' -q
	$(PYTHON) tools/test_mmu.py

.PHONY: test-mmu-board
test-mmu-board:
ifeq ($(IOP),storage)
	$(PYTHON) tools/test_storage.py
else
	$(PYTHON) tools/test_mmu_board.py --name tb_mmu_bus --out build/test-mmu-bus
	$(PYTHON) tools/test_mmu_board.py --name tb_mmu_disk --out build/test-mmu-disk
endif

.PHONY: test-storage test-storage-controllers bsd-image test-bsd storage-menu test-storage-menu
storage-menu:
	$(PYTHON) tools/build_storage_menu.py
test-storage-menu:
	$(PYTHON) -m unittest discover -s tests -p test_menu_image.py -q
	$(PYTHON) tools/test_storage_menu.py

test-storage:
	$(PYTHON) -m unittest discover -s tests -p test_storage_label.py -q
	$(PYTHON) tools/test_storage.py

bsd-image:
	@test "$(BOARD)/$(CPU)/$(FPP)/$(IOP)" = hc7000-lcd-sram/mmu/off/storage || { echo 'BSD requires HC7000 MMU, FPP=off, IOP=storage'; exit 1; }
	$(PYTHON) tools/build_sd_bsd.py --out $(BSD_OUT)
test-bsd:
	@test "$(BOARD)/$(CPU)/$(FPP)/$(IOP)" = hc7000-lcd-sram/mmu/off/storage || { echo 'BSD requires HC7000 MMU, FPP=off, IOP=storage'; exit 1; }
	$(PYTHON) tools/test_mmu_board.py --name tb_mmu_bsd --image $(BSD_OUT)/uj11-hc7000-2.9bsd.img --out build/test-bsd
test-storage-controllers:
	$(PYTHON) tools/test_storage_rl_xp.py
	$(PYTHON) tools/test_storage_rk_rq.py
	$(PYTHON) tools/test_mmu_board.py --name tb_storage_bus --image build/test-storage-rl-xp/controllers.img --out build/test-storage-bus
	$(IVERILOG) -g2012 -s tb_iop_bus -o build/test-storage-bus/iop-bus tests/mmu/tb_iop_bus.v boards/hc7000/mmu/uj11_mmu_iop_bus.v
	$(VVP) build/test-storage-bus/iop-bus
	$(IVERILOG) -g2012 -s tb_ubmap -o build/test-storage-bus/ubmap tests/mmu/tb_ubmap.v boards/hc7000/mmu/uj11_mmu_ubmap.v rtl/mmu/uj11_mmu_apr_ram.v
	$(VVP) build/test-storage-bus/ubmap

.PHONY: test-mmu-fpp
test-mmu-fpp:
	@test "$(FPP)" != off || { echo 'FPP tests require FPP=microcode'; exit 1; }
	$(PYTHON) tools/test_mmu_fpp.py
