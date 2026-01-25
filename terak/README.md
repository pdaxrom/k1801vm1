# TERAK‑like PDP‑11 emulator (C99)

This directory contains a **minimal** TERAK‑style PDP‑11 emulator built on top of the
read‑only `core/` CPU library (DCJ11 variant).  The goal is to be able to boot an
RT‑11 RK05 image while keeping the code base tiny and portable.

## What is ready

* **Project layout** – all emulator code lives under `terak/` (CPU core is untouched).
* **Makefile** – `make` builds a static `libcore.a` from `core/` and links the TERAK
  objects into `build/bin/terak`.
* **Bus abstraction** – 16‑bit address space (64 KB) with RAM (000000‑0177777) and an
  I/O page (160000‑177777).  Reads/writes are dispatched to device drivers.
* **DL11 console** – memory‑mapped UART at the standard octal addresses
  `177560` RCSR, `177562` RBUF, `177564` XCSR, `177566` XBUF.  Input is polled from the
  host terminal in raw, non‑blocking mode; output is printed to stdout.
* **RK11 stub** – CSR registers are present (`177400`‑`177412`).  The stub does not
  perform real disk I/O yet but provides the necessary registers for a host‑side
  bootstrap.
* **Adapter layer** – `adapter_core.c/h` wires the core callbacks (`load_byte`,
  `store_word`, …) to the bus implementation.
* **Machine layer** – `machine.c/h` creates the CPU, loads binaries, runs the
  execution loop and polls devices each step.
* **CLI** – `main.c` parses long options (`--load`, `--pc`, `--trace`, `--max-steps`).
* **Test program** – `test/hello.mac` prints `Hello` via the DL11 device and a
  simple Makefile to assemble it with the provided `macro11` tool.

## Project layout

All emulator sources are placed under `terak/`; the read‑only CPU core stays in
`../core/` and is never modified.

```
terak/
├─ Makefile                # builds libcore.a + terak binary
├─ README.md               # this document
├─ src/                    # core emulator implementation
│   ├─ main.c              # CLI entry point and option parsing
│   ├─ machine.c           # RAM allocation, execution loop, binary loader
│   ├─ machine.h
│   ├─ adapter_core.c      # glue between core/ callbacks and our bus
│   ├─ adapter_core.h
│   ├─ bus.c               # 16‑bit address translation, read/write helpers
│   ├─ bus.h
│   ├─ dl11.c              # minimal DL11 console (UART) implementation
│   ├─ dl11.h
│   ├─ rk11.c              # stub RK11 controller (CSR registers only)
│   ├─ rk11.h
│   ├─ boot.c              # placeholder for a future PDP‑11 bootstrap loader
│   ├─ boot.h
│   ├─ util_term.c         # raw terminal handling (non‑blocking stdin)
│   └─ util_term.h
├─ tools/                  # auxiliary utilities (optional)
│   └─ mk_rk05.c           # create / verify RK05 disk images
└─ test/                   # simple test program and build script
    ├─ hello.mac           # tiny program that writes "Hello" via DL11
    └─ Makefile            # assembles `hello.mac` using the provided macro11
```

The directory contains only C source files, a plain Makefile and a tiny test
program; no external libraries or build systems beyond GNU/BSD `make` are required.

## How to build & run

```bash
cd terak
make                # builds ./build/bin/terak
```

### Load a binary and set the PC

```bash
./build/bin/terak --load hello.bin --pc 000200
```

### RK05 boot (stub)

```bash
./build/bin/terak --rk05 rt11_rk05.dsk --boot rk
```

The current RK11 implementation does not transfer data; a host‑side bootstrap can
copy the first blocks into RAM before starting execution.

## TODO – what remains

* **Full RK11 implementation** – handle the GO flag, READ/WRITE commands, translate
  the disk address (`RKDA`) to a linear block number, and move data between the
  RK05 image file and RAM.
* **Bootstrap loader** – a small PDP‑11 program that reads the boot blocks via RK11
  and jumps to the RT‑11 monitor.
* **Interrupt support** – expose `poll_irq` and device interrupt lines so the core
  can service them.
* **More tests** – add smoke tests for the RK11 driver and for a real RT‑11 image.

## Memory map (octal)

```
000000‑0177777   RAM (56 KB)
160000‑177777   I/O page
    177560  DL11 RCSR
    177562  DL11 RBUF
    177564  DL11 XCSR
    177566  DL11 XBUF
    177400  RK11 CSR base (RKDS, RKER, RKCS, RKWC, RKBA, RKDA …)
```

All addresses in the source are written in octal (C literals with a leading `0`).
