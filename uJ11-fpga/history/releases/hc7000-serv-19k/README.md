# HC7000: 19 KiB SERV/SD memory

> Историческая запись: `releases/hc7000-serv-19k/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


2026-10-01 qualified source and synthesis checkpoint, **not programmed on the
physical board**. The board remains on PAL 2AAE. No physical SD or source guest
disk image was modified. PAL remains at 200 rows in this checkpoint.

Five EBRs recovered from J11 microcode (9 → 6), SERV RF (1 → distributed RAM)
and MMU PAR/PDR (1 → distributed RAM) expand SERV/SD memory from 14 to **19 KiB**.
Firmware gets **18.5 KiB**; the remaining 512 bytes are the SD/DMA sector buffer.
The first 18 KiB retain full-width 32-bit access, including the entire stack.
The final KiB uses two 16-bit accesses per word and provides optional cold BSS
plus the sector buffer. The peripheral controllers remain software in SERV.
See [memory layout and arbitration](../../../docs/serv-19k.md).

| Resource | Previous 14 KiB checkpoint | This checkpoint |
|---|---:|---:|
| LUT4 | 4992 | **5697 / 6864** |
| Available LUT4 | 1872 | **1167** |
| FF | 1806 | **1844 / 7209** |
| EBR | 26 | **26 / 26** |
| Free firmware space, including cold BSS | 1016 bytes | **6108 bytes** |
| CPU / PAL clock | 50 / 64 MHz | **50 / 64 MHz** |

[MAP/PAR/TRACE](synthesis/result.json) passes CPU, PAL, external SRAM and all
four PAL mailbox timing constraints with zero negative slack. Fmax is
**50.246 MHz CPU / 64.140 MHz PAL**. Microinstructions remain 54 bits with
the same addresses and one-cycle reads; FIS and microcoded ODT remain enabled.
FPP is disabled. No new hardware controller register banks were added.

| Validation | Result |
|---|---|
| [RAM](validation/ram/result.json) | 106125 checks per behavioral/official Lattice model; real SERV split RV32IC fetches across 16/18 KiB at 24/50 MHz and cold SW/LW |
| [MMU and vendor ROM](validation/mmu/result.json) | All 4096 microaddresses, 8192 ROM checks; MMU/cache/CSM/locked bus/interrupts/ODT pass |
| [Integer differential](validation/integer/result.json) | 2696 programs, 50599 checks against core/core.c with official ROM |
| [Addressing/EIS differential](validation/ea-eis/result.json) | 7888 programs, 141632 checks at 50 MHz including combined MMU aborts |
| [RK05/RQ with active PAL](validation/rk-rq/result.json) | 1029661 checks |
| [RL/RP](validation/rl-xp/result.json) | 368432 checks, 3819 DMA words |
| [RH and bad labels](validation/rh/result.json) | Primary/backup labels, CRC rejection and unsupported media pass |
| [Full-board SD menu](validation/menu/result.json) | All 14 scenarios pass: five controller types, timeout/Enter/cancel, bad menu and reset |
| [RT-11 XM](validation/os/rt11xm/result.json) | Five-second RH0 autoboot, 2 MiB / 22-bit configuration, storage RH1 and all three RK compiler disks; 6107 checks, 165086 DMA words |
| [RT-11 V4](validation/os/rt11v4/result.json) | Enter boot from RK0, DIR and TYPE; 2816 checks, 63885 DMA words |
| [Original RQ0](validation/os/rsx-rq0/result.json) | Expected nonbootable placeholder HALT at PC=000034, matching SIMH |
| [RSX-11M-PLUS RQ1](validation/os/rsx-rq1/result.json) | BL87 boot, date, DEV DU and PIP DU1:[1,54]RSX11M.SYS/LI; 3346 checks, 786362 DMA words |
| [2.9BSD](validation/bsd/result.json) | Multiuser root login, directories, fstab, file write/read on RL and RP, removal and sync; 22586 checks, 419799 DMA words |
| [Legacy SERV](validation/legacy/result.json) | 60949 RK611 checks; RV32I binary exactly matches the released firmware |
| [Firmware without PAL](validation/no-pal/firmware/build.json) | 11211 program bytes; 6116 bytes free including cold BSS |
| [HC1200 baseline](validation/hc1200.json) | Released RTL/ROM/software/BASIC/JED equivalence passes |

[Firmware](firmware/build.json): 11219 program bytes, 976 bytes BSS,
unchanged 640-byte stack. The cold-data region is currently empty; 5596 bytes
remain between BSS and the stack, plus 512 cold bytes. Stack guards observe
at most 336 bytes used in the controller/BSD tests. All 2 MiB external SRAM
remain available to PDP-11.

The [boot matrix](validation/os/result.json) uses active PAL, 50 MHz CPU and
the actual 50 Hz system clock. Source disks and regular SD backing files remain
unchanged; guest writes are kept in temporary overlays. The UART model supplies
no VT52/VT100 identity, so stock RSX terminal inquiry times out before date input
and the remaining startup commands.

JED (`releases/hc7000-serv-19k/hc7000-serv-19k.jed`), checksum **F5B5**:
`634ac5e9bf29c113685413c4f433fd12f426a7dae21c56001a91ac9c9b6cb255`.
It is exported from the source-matched timing-qualified build; physical board
qualification and speed measurements are pending.

`sources/` preserves synthesis inputs and test dependencies. `reference/`
preserves the CPU reference used by differential tests and the menu assembler.
`validation/runner/board.json` freezes generated inputs so parallel tests do
not regenerate each other's files. The runner changes only input generation;
the stored tools and testbench assertions execute normally. XM uses the real
five-second timer. `validation/runner/parallel_os.py` completes the remaining
independent OS cases with the existing testbench Enter input and the same
compiled RTL; no assertions or CPU/device models are changed. `SHA256SUMS`
covers this checkpoint, including the JED and logs. Large SD files and compiled
simulation executables are excluded; media records retain image/source hashes.

Reproduce from `uJ11-fpga` with the Linux RISC-V compiler or a verified firmware
cache. Use fresh output names, and do not run builders for different profiles
against the same generated directory concurrently:

```sh
export UJ11_MMU_IOP=storage UJ11_MMU_FPP=off UJ11_MMU_CLOCK_MHZ=50
export UJ11_HC7000_VIDEO=1 UJ11_HC7000_DIAGNOSTICS=1
python3 tools/test_storage_ram.py --out build/recheck-19k-ram --vendor-library /path/to/diamond/cae_library/simulation/verilog/machxo2
python3 tools/test_mmu.py --out build/recheck-19k-mmu --vendor-library /path/to/diamond/cae_library/simulation/verilog/machxo2
python3 tools/test_storage.py
python3 tools/test_storage_rk_rq.py --fast-memory
python3 tools/test_storage_rl_xp.py
python3 tools/test_storage_menu.py
python3 tools/build_storage_menu.py --out build/recheck-19k-menu
python3 tools/test_os_boot_rtl.py --out build/recheck-19k-os --menu build/recheck-19k-menu/menu.img
python3 tools/synthesis_mmu.py serv-19k-recheck
python3 tools/export_jed.py serv-19k-recheck
```
