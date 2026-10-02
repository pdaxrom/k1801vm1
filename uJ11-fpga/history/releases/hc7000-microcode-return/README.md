# HC7000 MMU: ALU/RETURN, 50 MHz

> Историческая запись: `releases/hc7000-microcode-return/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


The final ALU operation of selected microcode routines now returns through the
existing return stack in the same microinstruction. Memory-source addressing
tail-dispatches destination addressing. Register-source sampling, MMU abort
ordering and post-write flag updates are preserved.

The production profile is **50 MHz, FPP off, SERV storage IOP**, with the
**12 MHz external oscillator** and IS61WV102416BLL-10T SRAM. Routed resources
remain **4239 LUT4, 1441 FF, 26 EBR**. Fmax is **51.148 MHz**; the 50 MHz clock
and external SRAM constraints pass. Microcode uses **1162 words**, down from
1169 in JED 7123. HC1200/MMU-less and SERV firmware are unchanged.

JED checksum **1B7F**, SHA-256
`1cfce5454efa6001b2ff75086af04b47849f71bfba311663cb92be8e6b75863a`.
**JED 1B7F is installed on the physical board. Flash Verify and the BSD
multiuser/RL/XP checks passed.** See [installation evidence](hardware-1b7f/README.md).

Relative to 7123, synthetic mapped CPU loops improve by 3.9% for memory
operations, 1.1% for the EIS mix and 8.3% for word copy; register operations
are unchanged. In complete-board RTL profiling, eight BSD commands improve
by 2.2–5.5% with `stty nl0 cr0`. The profile includes SERV, SD, SRAM and UART;
these are simulation measurements, not measured speedups on physical media.
`qualification.json` contains exact before/after command counts.

The C reference also fixes combined MMU length/access abort causes in both
ordinary and previous-space translation. A dedicated 64-case instruction test
checks MMR0/1/2, first-fault freeze, PDR.W and absence of faulted writes.
The old simultaneous-cause C/RTL discrepancy is resolved in this release.

Validation evidence in `validation/` includes:

- MMU/CPU/ODT/CSM/locked-DMA/event tests at 50 MHz, also with Lattice EBR models;
- the 24 MHz microcoded-FPP regression and 12 build-profile tests;
- 2696 integer programs / 50599 comparisons at 50 MHz;
- 7888 addressing/EIS programs / 141632 comparisons at 24/50 MHz with combined
  abort causes, plus the default isolated-cause set at 50 MHz;
- the C-core test matrix with MMU enabled and disabled;
- full BSD multiuser login, RL/XP read/write/remove/sync, RT-11 XM and V4
  boot/commands, and RSX STARTUP, `DEV DU:` and
  `PIP DU1:[1,54]RSX11M.SYS/LI` (1026 blocks);
- 47 HC1200 source hashes and six generated files unchanged from baseline.

Full OS tests use Verilator and behavioral memories. The Lattice-model
qualification is the CPU/MMU suite; it is not a complete OS boot test.
The installed SERV image remains 10515 bytes, RV32IC, SHA-256
`2b81f76ddbe3c3588c34c0489f8d3f545787760c5162e85e8e9f84e158449468`.

`baseline-physical-7123/` is separate evidence for the physical installation
and BSD/RSX tests of the previous JED. Flash verification succeeded; an early
getty input was flushed, so the successful multiuser retry is recorded in
`bsd-7123-tests/`. The timing run completed its measured commands but its
subsequent `/etc/halt` probe failed because that file is absent. A separate
SIMH run reproduces `/etc/reboot -h` returning `Invalid argument` in these
source disks. These raw failed session statuses are preserved, not relabeled.
Physical RSX testing subsequently passed on 7123 after a long button RESET:
STARTUP, DU0/DU1 and the 1026-block directory entry were verified. See
[physical RSX evidence](baseline-physical-7123/rsx/README.md), including the
SD readback checks, preserved BSD hashes and two earlier failed captures.

`sources/` freezes synthesis inputs and test sources. Generated production
ROMs are under `sources/uJ11-fpga/build/`; the separate 24 MHz/FPP ROMs used in
that regression are under `validation/mmu-fpp24/generated/`. One early combined-abort
run predates a docstring-only assembler comment; its exact assembler is retained
in `validation/ea-eis-combined-50/assembler-used.py`. The frozen 7123
microcode/profile baseline is identified by hashes and its original release;
its profiling bench and runner are retained under `validation/bsd-baseline/`.
Content-addressed gzip fixtures in `fixtures/` are named by uncompressed
SHA-256. Disk images and licensed Lattice models are identified by hash and
are not redistributed here. `SHA256SUMS` covers every packaged file.

See [implementation, measurements and profiling commands](../../../docs/hc7000-cpu-performance.md)
and the [BSD console settings](../../../docs/bsd-console.md).
