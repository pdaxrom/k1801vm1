# HC7000 MMU / SERV partitioned-SD candidate

> Историческая запись: `releases/hc7000-serv-storage/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


This is an archived, partition-only build, **not flashed or physically qualified**.
The subsequent `../hc7000-serv-menu/` release includes the menu and has passed
physical-board qualification. This earlier build needs a partitioned SD image;
do not use it as a drop-in update for an existing raw card.

Selection: `BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage`.
See `../../docs/serv-storage.md` for format, utility commands, limitations and
reproduction. Only RK07/RH11 is implemented in this firmware. The other types
are reserved in the table and host utility. This snapshot has no interactive menu.

- SERV code 3892 bytes, BSS 788 bytes; 8 KiB EBR RAM, 1024-byte reserved stack.
- 3366 LUT4 / 6864, 1103 FF, 24 EBR / 26.
- MAP/PAR/TRACE: 24 MHz constraints passed; Fmax 25.713 MHz.
- All final synthesis source and generated-input hashes matched locally.
- Real SERV/SPI/SRAM integration: primary table, backup recovery, both copies
  invalid and unsupported boot type; bounds, readonly, cancellation and DMA wrap.
- Lattice EBR models: 14593 checks across all words, lanes and banks.
- Full RT-11XM cold boot from unit 7: 3355 checks, 541857264 clocks,
  114330 DMA words, 14073931 mapped fetches; directory and memory commands.
- Retained MMU legacy mode: bus 48 checks, disk 60949 checks.
- HC1200: 47 protected source hashes unchanged; profile/format host tests passed.

`qualification.json` hashes the preserved evidence. The boot image is derived
from `releases/sd-hc7000/uj11-hc7000-rt11xm.img.gz`; its label copies are stored
in `validation/rt11xm-unit7/sd-label.bin`. Full test SD images are reproducible
and are not duplicated here. Simulation opens backing images read-only.
The cold-boot test preceded a whitespace-only builder cleanup; all hardware,
firmware, generated ROM and RAM sources are identical to the final build.

Known first-stage cost: controller clear restarts SERV and revalidates the SD
label. Before adding independent controllers, replace that reset with separate
command cancellation so that one controller cannot reset another's service.

The repository ignores `*.log` globally. Preserve the logs in this directory
when committing a release (explicit `git add -f` is needed for those files).
