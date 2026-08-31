# LSI-11M AM4 FPGA for HC1200

This directory is the standalone FPGA port of the recovered AM4/LSI-11M
processor to the HC1200 board. It is intentionally separate from `microcpu`:
the CPU is the original Am2900-style AM4 datapath and 56-bit MicROM, with a
direct request/ready bus adapter, external SPI FRAM as PDP-11 memory, and a
small SD/RK611 integration layer for booting RT-11.

The currently verified configuration provides:

- AM4/LSI-11M CPU with the recovered 1024 x 56 control store;
- a MicROM fix for store-only `MOVB memory,memory` destination cycles;
- retained ODT over the console UART;
- KL11-compatible console at `177560`, vectors `060` and `064`, 115200 8N1;
- 50 Hz EVNT interrupt at vector `100` after RT-11 installs its handler;
- guest memory in external SPI FRAM;
- reset bootstrap in otherwise unused physical bits of the seven MicROM EBRs;
- SDHC initialization and two-sector RT-11 handoff;
- software RK611 at `177440..177476`, including SD `CMD17` and `CMD24`;
- a Diamond project for `LCMXO2-1200HC-4SG32C`.

## Quick start

Python 3, GNU Make, Icarus Verilog and `microasm11` are required. Verilator is
used for full RT-11 simulation. Lattice Diamond 3.14 is needed for FPGA build.
`microasm11` is expected in the sibling directory by default:

```text
k1801vm1/
├── microasm11/microasm11
└── lsi11-fpga/
```

Build firmware and the seven-EBR control-store module, then run regression:

```sh
make
make test
make test-rt11
```

Paths may be overridden:

```sh
make test ASM11=/absolute/path/to/microasm11
make test-rt11 RT11_IMAGE=/absolute/path/to/rt11v503.dsk
```

On a Linux host with Diamond 3.14:

```sh
make test-vendor
make diamond
make program
```

`make diamond` never programs hardware. `make program` generates an XCF from
the current JED, including timestamp and JEDEC checksum, then performs FLASH
erase/program/verify.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [MicROM source and modifications](docs/MICROCODE.md)
- [SD bootstrap and RK611 service](docs/SD-BOOT-RK611.md)
- [Build, Diamond, Programmer and UART](docs/BUILD-DEBUG-PROGRAM.md)
- [Regression coverage and limitations](docs/TESTING.md)
- [Port history and design decisions](docs/PORTING-NOTES.md)

## Directory layout

```text
boards/hc1200-microcomp/  board top, bus adapter, LPF, strategy, Diamond files
rtl/experimental/am4/    active AM4 direct-bus core and behavioral MicROM
rtl/experimental/lsi11/  console UART implementations reused from cpu11
rtl/                      SD byte service and SPI FRAM memory controller
ucode/experimental/am4/  MicROM source/tools, bootstrap and RK611 service
testbench/                Icarus/Verilator tests and SD/FRAM models
scripts/                  MicROM-diff and Programmer-XCF helpers
images/                   local operating-system media; ignored by Git
docs/                     design, build and debugging documentation
```

## Provenance and license

The AM4 RTL and recovered MicROM originate from
[`1801BM1/cpu11`](https://github.com/1801BM1/cpu11), commit
`e0576637a35f0378f85673213a808098b9535526`. Original copyright headers are
retained. Upstream material is CC BY 3.0; the complete license is in
[`rtl/experimental/am4/UPSTREAM-LICENSE.md`](rtl/experimental/am4/UPSTREAM-LICENSE.md).

The RT-11 disk image is development media and is ignored by Git. Its presence
in a working tree does not change its original license.
