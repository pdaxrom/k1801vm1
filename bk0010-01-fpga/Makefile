PYTHON ?= python3
DCCA_MODEL ?= build/DCCA.v
DIAMOND_PROJECT ?= build/diamond

.PHONY: all build test test-boot test-soc test-soc-diamond test-all test-plm test-reference diamond diamond-check diamond-export
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
diamond: build
	$(PYTHON) tools/diamond_project.py --out $(DIAMOND_PROJECT)
diamond-check:
	$(PYTHON) tools/diamond_report.py check $(DIAMOND_PROJECT)
diamond-export:
	$(PYTHON) tools/diamond_report.py export $(DIAMOND_PROJECT)
