# RH11 vs RH70 Baseline (v1 draft)

This note captures controller-level deltas that drive the RH70 v1 plan.

## Sources used

- SIMH reference implementation: `/Users/sash/Work/tmp/simh/PDP11/pdp11_rh.c`
- DEC background docs used for cross-check context:
  - `/Users/sash/Work/PROJECTS/k1801vm1/doc/EK-KDJ1B-UG_KDJ11-B_Nov86.pdf`
  - `/Users/sash/Work/PROJECTS/k1801vm1/lsi11/docs/EK-RK067-UG-001.pdf`

## RH11 vs RH70 delta matrix

| Area | RH11 baseline | RH70 target behavior | SIMH anchor |
| --- | --- | --- | --- |
| Register tail decode | Last 2 words of CSR block are not decoded as MBA regs | Last 2 words decode as `BAE`/`CS3` | `mba_map_pa`: RH11 returns NXM, RH70 maps to `BAE_OF` (`pdp11_rh.c` around lines 800-805) |
| DMA address width | `BA` + `CS1<BA16:BA17>` (18-bit path) | `BA` + `BAE<5:0>` (up to 22-bit bus path) | `CS1_UAE`/`BAE` defs and `ba = (bae << 16) | ba` in DMA helpers |
| CS1/BAE coupling | CS1 high-byte updates 2 UAE bits | RH70 keeps full BAE register; CS1 mirrors low UAE subset | CS1 writes update BAE, BAE writes update CS1 (`pdp11_rh.c` around lines 465-466, 512-513) |
| CS1/CS3 IE coupling | IE is effectively on CS1 path | RH70 mirrors IE through CS3 too | CS3 read/write mirrors CS1 IE (`pdp11_rh.c` around lines 387-388, 520-521) |
| BA increment + inhibit | BA increments unless `CS2_UAI` set | Same base rule, but overflow/carry semantics go through wider BAE path | DMA loops (`pdp11_rh.c` around lines 566/605/649 and 572-575/611-614/655-658) |
| GO/program error | Transfer command with `GO` while `DONE=0` raises `PGE` | Same check applies | `cs1dt` + `DONE` gate to `CS2_PGE` (`pdp11_rh.c` around lines 444-446) |
| NXM/error propagation | NXM sets `CS2_NEM`; completion sets DONE with error state visible in CS1/CS2 | Same class, with RH70 register model | NXM in DMA helpers + `mba_upd_cs1` error folding (`pdp11_rh.c` around lines 557/596/634, 734-737) |
| DONE/IE/IRQ semantics | DONE edge + IE and SC path feed interrupt request | Same logic, including CSTB/IFF behavior | File header warning block + `mba_upd_cs1` + `mba*_inta` (`pdp11_rh.c` lines near 35-49, 731-741, 760-778) |
| Controller clear | `CS2_CLR` resets controller state | Same class, RH70 state includes BAE/CS3 reset | `CS2_CLR` path + `mba_reset` (`pdp11_rh.c` around lines 488-490, 821-828) |

## Branch status for phase 1 (items 2-4)

- Added controller mode plumbing (`rh_mode = rh11 | rh70`) and API surface.
- Added CLI configuration: `-rh-mode rh11|rh70`, default `rh11`.
- Added config visibility (`-check-config` now prints `rh_mode=`).
- Implemented RH70 data path baseline:
  - `RHBAE` (`0177474`) driven 22-bit DMA address construction
  - BA overflow carry into BAE (6-bit extension path)
  - `pdp1184` compatibility path: when `UBMAP` is enabled and `BAE<5:2> == 0`,
    legacy RH11-style DMA (`CS1<BA16:BA17>`) is translated through UBMAP
  - NXM/error completion behavior validated in RH70 mode tests
- Implemented RH70 control baseline subset:
  - `CS3<IE>` mirrors into `CS1<IE>` and back
  - controller clear (`CCLR`) resets IE/IRQ state without spurious IRQ
  - repeated `GO` while command is active raises `PGE` and completes command
- Added automated mini SIMH differential check:
  - test: `lsi11/tests/test_rh70_simh_compare.sh`
  - compares masked `CS1/CS2` step semantics (`DONE/IE/GO`, `IR`, `PGE`) between SIMH HK and local RH70 mode
- Added optional ULTRIX smoke helper:
  - test: `lsi11/tests/test_ultrix_rh_smoke.sh`
  - checks `rh11` and `rh70` path both reach ULTRIX setup on `disks/ultrix/sys.dsk`
- Remaining RH70 work is concentrated in control/IRQ corner-case parity with reference behavior.
