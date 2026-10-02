# HC7000 MMU 50 MHz

> Историческая запись: `releases/hc7000-50mhz/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


Build-time 50 MHz profile for LCMXO2-7000HC-4TG144C, the 12 MHz external
oscillator and IS61WV102416BLL-10T SRAM. FPP is disabled; disk controllers
remain software running on SERV RV32IC. The default build remains 24 MHz.

The CPU adds a preparation clock for operands and microinstruction controls.
A shorter MMU opcode decoder and control paths, registered sparse-ROM region
selection, and an instruction-response register before SERV's RV32C decoder
allow the new clock. The 24 MHz CPU bypasses the preparation stage. HC1200
and its generated outputs remain unchanged.

| Configuration | LUT4 | FF | EBR | Routed Fmax |
|---|---:|---:|---:|---:|
| Previous qualified 24 MHz CPU-events build | 4173 | 1205 | 26 | 26.263 MHz |
| Current sources at 24 MHz | 4057 | 1205 | 26 | 25.505 MHz |
| Current sources at 50 MHz | 4137 | 1348 | 26 | 51.459 MHz |

Both current profiles pass MAP/PAR/TRACE at their requested frequency,
including external SRAM constraints. The 50 MHz build closes timing on
placement seed 2 with 0.567 ns worst setup slack. No internal multicycle
exceptions were added. Microcode remains 1189 words in nine EBRs; SERV's
firmware remains 10515 bytes. Total EBR use remains 26/26.

Measured RTL workload speedups are 1.735× for register operations, 1.553×
for memory operations and 1.146× for an EIS-heavy loop, with matching final
architectural state. Preparation cycles mean the frequency ratio is not
the instruction-throughput ratio. These are CPU test loops, not physical
board or whole-OS benchmarks.

Validation passes:

- 2696 differential programs against `core/core.c`: 50599 comparisons, with
  Lattice EBR models and the final 50 MHz CPU/ROM sources.
- 1048576 decoder combinations, ROM, MMU translation/transactions, CPU/ODT,
  CSM, locked accesses with DMA and 233 CPU-event checks at both 24 and 50 MHz.
- The 24 MHz microcoded-FPP profile and all 11 build-profile tests pass.
- Four SRAM pin-delay corners, 37 checks each, including byte writes and resets.
- HC1200: 47 source files and six generated outputs match baseline `9d345ce`.
- RT-11 XM from RH0 with RH1 and RK0/RK1/RK2 directory checks; RT-11 V4 from RK0.
- 2.9BSD from RL0, RL1 swap and XP0 `/usr`: multiuser root login, directory and
  `fstab` reads, file writes/readback on RL and RP, cleanup and `sync`.
- RSX-11M-PLUS V4.6 BL87 from RQ1: complete STARTUP, DU0/DU1 device listing,
  and `PIP DU1:[1,54]RSX11M.SYS/LI` showing `RSX11M.SYS;1`, 1026 blocks.

The RT-11 tests use the saved bench in `validation/boot-rt/test-sources/`.
After they passed, only the bench timeout scaling and counter width were
changed to preserve its 83-second budget at 50 MHz; all assertions and RTL
inputs are identical. RSX qualification uses the widened counters and completes
after 2194131267 clocks, exceeding both the old timeout and signed 32-bit range.

`qualification.json` indexes the results and source hashes. `SHA256SUMS`
covers this release, including validation logs and the saved test-source variant.

The preserved JED is `uj11-hc7000-mmu-50mhz.jed`, checksum **74B5**, SHA-256
`ea7e6da4cd9cc5e8012ded5596d6f03964c097f58865db98e0fb8b35c8f14d53`.
`synthesis/` contains the original build inputs, clock constraints, strategy,
export record and reports. `sources/` preserves their exact source files and
generated ROM/firmware inputs. Disk images are referenced by hashes rather
than duplicated here. Lattice's private simulation models are not redistributed.

**The 50 MHz JED has not been programmed onto the physical board.** The board
remains on the qualified 24 MHz CPU-events C820 image. SRAM timing uses an
assumed 1 ns PCB flight time each way; no oscilloscope measurement is claimed.

Rebuild on the Linux machine with Diamond:

```sh
make CPU=mmu BOARD=hc7000-lcd-sram FPP=off IOP=storage MMU_CLOCK_MHZ=50 synthesis
make CPU=mmu BOARD=hc7000-lcd-sram FPP=off IOP=storage MMU_CLOCK_MHZ=50 export
```

See [clock, timing and performance details](../../../docs/hc7000-50mhz.md).
