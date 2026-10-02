# HC7000: shared SERV and SD/DMA memory

> Историческая запись: `releases/hc7000-serv-shared/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


2026-10-01 source and synthesis checkpoint. **Not programmed on the board**;
the board still runs PAL 2AAE. PAL remains at 200 rows in this checkpoint.
No physical SD card or guest disk image was changed.

SERV and the sector engine share fourteen EBRs: **14 KiB physical memory**,
with **13.5 KiB for firmware/data/stack** and **512 bytes for the sector buffer**.
This recovers the unused half of the previous sector EBR and replaces the
serialized final KiB of SERV RAM with a full-width 32-bit bank. The firmware
has 1016 free bytes between BSS and its unchanged 640-byte stack.

The sector engine has priority only in the final 2 KiB bank. Received words
are committed during the existing SPI/DMA gap cycle, removing the reply signal
from the EBR arbitration path without adding a transfer cycle. SERV can still
execute and service peripheral requests while a disk transfer is active.
See [architecture, addresses and timing](../../../docs/serv-shared-ram.md).

| Resource | Previous 13 KiB checkpoint | This checkpoint |
|---|---:|---:|
| LUT4 | 4942 | **4992 / 6864** |
| Available LUT4 | 1922 | **1872** |
| FF | 1838 | **1806 / 7209** |
| EBR | 26 | **26 / 26** |
| CPU / PAL clock | 50 / 64 MHz | **50 / 64 MHz** |

[MAP/PAR/TRACE](synthesis/result.json) passes all CPU, PAL, SRAM-pin and PAL
mailbox timing constraints with zero negative slack. CPU Fmax is 50.332 MHz;
PAL Fmax is 64.906 MHz. The installed 2AAE uses 4911 LUT; this checkpoint
adds 81 LUT in total, including the earlier bootstrap relocation.

| Validation | Result |
|---|---|
| [Shared RAM](validation/ram/result.json) | 75784 checks in each of behavioral and official Lattice models; real SERV split instruction fetches at 24/50 MHz |
| [RK05/RQ with active PAL](validation/rk-rq/result.json) | 1029350 checks; partial writes, zero padding, compare, mapped DMA and resets |
| [RL/XP](validation/rl-xp/result.json) | 367987 checks, 3819 DMA words; partial RL sectors and controller cancellation |
| [RH and bad labels](validation/storage/result.json) | Four cases; CRC rejection, backup label, invalid and unsupported media |
| [Full-board menu](validation/menu/result.json) | All 14 scenarios: five controller types, timeout/Enter/cancel, corrupt menu and resets |
| [RT-11 V4 full-board boot](validation/rt11v4/result.json) | Actual five-second RK0 autoboot, DIR and TYPE; 2816 checks, 63885 DMA words; original disk image unchanged |
| [Legacy IOP](validation/legacy/result.json) | 60949 checks; firmware binary exactly matches the released RV32I image |
| [Firmware without PAL](validation/no-pal/firmware/build.json) | 11183 program bytes, 1024 bytes free between BSS and stack |
| [HC1200 baseline](validation/hc1200.log) | Released RTL/ROM/software equivalence passes |

Firmware with PAL: 11191 program bytes, 976 bytes BSS, 640-byte stack.
SHA-256: `5b1f738bdd6522fcdca292ca1be03207af0ef4201d998fe602f758a196ce8951`.
The sector range is `0x3600..0x37ff`; it is reserved by the linker and excluded
from BSS clearing. Stack guards observe at most 336 bytes used in the controller
tests. All 2 MiB of external SRAM remain available to PDP-11.

`sources/` preserves the exact synthesis inputs, including the generated top
with diagnostic displays enabled. RTL tests instantiate the board or disk
directly; their unused generated top has diagnostics disabled and is separately
preserved in `validation/test-inputs/`. `SHA256SUMS` covers this checkpoint.
No JED is published here; physical qualification is pending.

Reproduce from `uJ11-fpga`:

```sh
export UJ11_MMU_IOP=storage UJ11_MMU_FPP=off UJ11_MMU_CLOCK_MHZ=50
export UJ11_HC7000_VIDEO=1 UJ11_HC7000_DIAGNOSTICS=1
python3 tools/test_storage_ram.py --vendor-library /path/to/diamond/cae_library/simulation/verilog/machxo2
python3 tools/test_storage.py
python3 tools/test_storage_rk_rq.py --fast-memory
python3 tools/test_storage_rl_xp.py
python3 tools/test_storage_menu.py
python3 tools/synthesis_mmu.py serv-shared-new
```
