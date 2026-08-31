# Build, debug, Diamond and programming

## Requirements

Assembly/simulation needs Python 3, GNU Make, Icarus Verilog, Verilator and
`microasm11` (default `../microasm11/microasm11`). Hardware build needs Linux
x86-64, Diamond 3.14, Programmer 3.14 and HC1200.

Tested installations:

```text
~/.local/lscc/diamond/3.14
~/.local/lscc/programmer/diamond/3.14
```

## Firmware build

```sh
make clean
make
```

Outputs include assembled/listed MicROM, bootstrap and RK service under
`build/` and `boards/hc1200-microcomp/`, plus `am4_mcrom_ebr.v` and
`am4_sd_boot.rom`. Assembly errors are saved to adjacent `*.asm.log` files.

## Simulation

```sh
make test-fast
make test
make test-rt11 RT11_IMAGE=images/rt11v503.dsk
make test-vendor
```

The SD model opens raw media read-only and keeps writes in a bounded RAM
overlay, so simulation cannot damage the image.

The normal RK611 READ/WRITE regression uses Verilator because the deliberately
long, spaced SD programming waits are expensive in interpreted Icarus. The
same bench can still be run under Icarus with
`make -C testbench am4-rk-test-icarus`; its timeout covers the full wait.

## Diamond

```sh
make diamond
```

Override installation if required:

```sh
make diamond DIAMOND_HOME=/opt/lscc/diamond/3.14 \
  DIAMOND_LIBSTDCPP=/lib/x86_64-linux-gnu/libstdc++.so.6
```

`build-am4.tcl` runs synthesis, translate, map, PAR and TRACE. It refuses to
export JED if cumulative negative slack is nonzero, and never programs the
board. The verified standalone gate is 640/640 slices, 1272/1280 LUT4s,
7/7 EBRs, zero unrouted connections, 5.052 ns setup slack, 0.304 ns hold
slack and zero setup/hold errors.  Resource totals must come from a clean
implementation directory; an incremental no-op is not evidence of fit.

For a remote Diamond host:

```sh
rsync -a --exclude build --exclude impl1-am4 ./ sashz-ubuntu:/tmp/lsi11-fpga/
ssh sashz-ubuntu 'cd /tmp/lsi11-fpga && make clean && make test && make test-rt11'
ssh sashz-ubuntu 'cd /tmp/lsi11-fpga && make test-vendor && make diamond'
```

## Programming

```sh
make program
```

This generates `build/hc1200-am4.xcf` from the current JED, extracting its
timestamp and checksum, then calls Programmer for FLASH erase/program/verify.
Never reuse an XCF from a different JED.

## UART verification

On the tested Linux host:

```sh
stty -F /dev/ttyUSB1 115200 cs8 -cstopb -parenb raw -echo
timeout 60s cat /dev/ttyUSB1 | tee /tmp/lsi11-fpga-uart.log
```

Expected output contains `RT-11FB (S) V05.03` and monitor prompt `.`. Send
commands slowly because the UART has one receive holding register:

```sh
printf D >/dev/ttyUSB1; sleep 0.2
printf I >/dev/ttyUSB1; sleep 0.2
printf R >/dev/ttyUSB1; sleep 0.2
printf '\r' >/dev/ttyUSB1
```

## Failure triage

- ODT only: inspect `157774` and `157776` for SD value and bootstrap stage.
- RT-11 banner without prompt: verify MOVB destination reads, `E5` response
  decoding, CS boundaries, busy completion and RTI `160476`.
- No UART: check LPF, 26.6 MHz clock, 115200 8N1 and `/dev/ttyUSB1` ownership.
- Simulation passes/hardware fails: run vendor EBR test, inspect TRACE, compare
  programmed JED SHA-256, and capture UART before programming.
