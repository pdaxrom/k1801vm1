# HC7000 MMU: shorter addressing and EIS microcode, 50 MHz

> Историческая запись: `releases/hc7000-microcode-fast/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


Shared two-operand EA sequencing preserves late register-source sampling.
Memory destinations use the existing MDR ALU input, retaining post-write
flag updates. MUL/DIV handle two bits per counter update. No RTL, board,
SERV firmware or HC1200 changes relative to the PAR/PDR cache release.

Routed resources remain **4239 LUT4, 1441 FF, 26 EBR**; Fmax **50.345 MHz**.
The 50 MHz clock and external SRAM timing constraints pass. Microcode uses
**1169 words**, down from 1185. Relative to JED 6BC0, mapped RTL loops improve
by 5.7% (memory), 13.0% (EIS mix), and 7.7% (word copy); register loop unchanged.
These are synthetic RTL measurements, not physical-board speed measurements.

Validation: 2696 integer differential programs / 50599 checks at 24/50 MHz,
including Lattice EBR models at 50 MHz; 7888 EA/EIS programs / 141632 checks
at 24/50 MHz (and on the 6BC0 baseline); MMU/CPU/ODT/CSM/event checks; 24 MHz
microcoded-FPP regression; 11 build-profile tests; complete BSD, RT-11 XM,
RT-11 V4 and RSX boot/command/disk scenarios. See logs in `validation/`.

Known pre-existing reference difference: simultaneous nonresident/length
faults produce MMR0 140005 in RTL (matching DEC/SIMH), 040005 in C-core.
The explicit `--combined-abort` reproducer fails identically on both releases;
logs are under `known-differences/`. The default passing test isolates causes.

JED checksum **7123**, SHA-256
`839a0257c9c9110a08861259eafa05c80e73750640fa69917070ebd17320e4f5`.
**This JED has not been installed on the physical board.**

`sources/` freezes synthesized and tested inputs. `baseline/` contains the
6BC0 microcode and a rerun with the same four-workload benchmark as this
release. Other baseline CPU sources are unchanged and included in `sources/`.
Disk images are identified by hash; Lattice models are not redistributed.
Deduplicated gzip fixtures are in `fixtures/`, named by the SHA-256 of their
uncompressed contents; individual result records identify the fixture hash.

See [changes and measurements](../../../docs/hc7000-cpu-performance.md) and
the user-confirmed [BSD console delay fix](../../../docs/bsd-console.md).
