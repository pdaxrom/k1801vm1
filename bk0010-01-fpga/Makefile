PYTHON ?= python3
DCCA_MODEL ?= build/DCCA.v
DIAMOND_PROJECT ?= build/diamond

.PHONY: all build test test-audio test-boot test-soc test-soc-diamond test-all test-plm test-reference test-models test-fdd test-profiles test-bios test-bios-ps2 test-bios-logical test-ebr-bios test-storage test-disk-os diamond diamond-check diamond-export
all: build
build:
	$(PYTHON) tools/build.py build
test:
	$(PYTHON) tools/build.py test --group fast
test-boot:
	$(PYTHON) tools/build.py test --group boot
test-soc:
	$(PYTHON) tools/build.py test --group soc
test-soc-diamond:
	$(PYTHON) tools/build.py test --group soc --dcca-model $(DCCA_MODEL)
test-all:
	$(PYTHON) tools/build.py test --group all
test-plm:
	$(PYTHON) tools/build.py test --group plm
test-reference:
	$(PYTHON) tools/build.py test --group reference
test-models:
	$(PYTHON) tools/build.py test --group models
test-audio:
	$(PYTHON) tools/build.py test --group audio
test-fdd:
	$(PYTHON) tools/build.py test --group fdd
test-profiles:
	$(PYTHON) tools/build.py test --group profiles
test-bios:
	$(PYTHON) tools/build.py test --group bios
test-bios-ps2:
	$(PYTHON) tools/build.py test --group bios-ps2
test-bios-logical:
	$(PYTHON) tools/build.py test --group bios-logical
test-ebr-bios:
	$(PYTHON) tools/build.py test --group ebr-bios
test-storage:
	$(PYTHON) tools/build.py test --group storage
DISK_SPEEDUP ?= 1
test-disk-os:
	$(PYTHON) tools/build.py test --group disk-os --disk "$(DISK)" --disk-speedup $(DISK_SPEEDUP) $(if $(DISK_A16M),--disk-a16m) $(if $(SECOND_DISK),--second-disk "$(SECOND_DISK)") $(if $(DISK_STOP),--disk-stop)
diamond: build
	$(PYTHON) tools/diamond_project.py --out $(DIAMOND_PROJECT)
diamond-check:
	$(PYTHON) tools/diamond_report.py check $(DIAMOND_PROJECT)
diamond-export:
	$(PYTHON) tools/diamond_report.py export $(DIAMOND_PROJECT)
