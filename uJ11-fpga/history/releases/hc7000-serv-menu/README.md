# HC7000 SERV: SD menu with timed automatic boot

> Историческая запись: `releases/hc7000-serv-menu/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


Hardware-qualified for `BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage`.
Programmed into the physical board with successful FLASH Erase/Program/Verify.
Its original 7,988,051,968-byte raw SD card was fully backed up and the compressed
backup verified by decompression and SHA-256 comparison. The tested 32 MiB
partitioned image was then written and read back; its SHA-256, both label copies
and menu checks passed. Backup location and checksums are recorded in
`hardware/sd-preparation.json`.

Physical checks (`hardware/qualification.json` and complete UART captures):

- Cold boot and long RESET load RT-11XM from DM7, with 2 MiB and 22-bit MMU.
- Automatic boot starts after 4.987 seconds; Enter starts it within 0.021 seconds.
- Another key cancels the timer; the menu still waits after six seconds.
  Absent controller/unit selections are rejected, then manual RH7 boot succeeds.
- A 116-sector SD copy compares identically with BINCOM; the scratch file is removed.
- HGX host-to-SD-to-host transfer preserves all 17021 bytes and zero padding to
  34 sectors. HGTIME date/time sync passes with a one-second measured difference.
- SETF traps through vector 010 and XM continues: FPP is disabled. The older kit
  welcome text and RT-11 configuration display still mention floating point;
  they are not a capability probe for this build.

The first flash harness sent a command before the startup file had finished;
its timeout is retained in the evidence. Subsequent checks wait for the final
startup prompt. `hardware_mmu.py --boot-ready "HC7000 XM KIT READY"` enables
this explicit wait on future programming runs. The first SD cleanup also
encountered the copied system file's inherited protection; the separate cleanup
capture confirms its unprotection and deletion. Neither required a firmware change.
Only the single RH7 configuration was exercised physically; multiple drives,
no-default and malformed-card cases remain covered by simulation.

The repository's pinned `microasm11` revision `e8b3ca8` also reproduces the
qualified menu binary and packaged image byte-for-byte; see
`validation/host/pinned-assembler.json`. No assembler submodule update is required.

The SD menu is adapted from `lsi11/demo/boot_menu.asm`. It lists the available
controllers and RH units. A partition's `bootable` flag selects the default:
wait five seconds to boot it, press Enter to boot immediately, or press another
key to cancel the timer and choose a disk. Without a default the menu waits.
Only RH11/RK07 controllers are implemented by SERV at this stage.

- RV32I firmware: 3976 code bytes; 8 KiB EBR RAM.
- PDP bootstrap: 430 bytes, still within the same boot ROM EBR.
- Menu: 2596 bytes, six SD payload sectors, loaded at PDP 0100000 in SRAM.
- 3366 LUT4, 1103 FF, 24/26 EBR; unchanged 24 MHz system clock.
- MAP/PAR/TRACE passed, Fmax 25.713 MHz; exported JED checksum F229.

Validation includes five-second countdown (250 KW11 ticks), immediate Enter,
cancellation and choosing unit 0 instead of default unit 7, absent controllers
and units, no-default operation, old direct boot, and rejection of corrupt
header, payload, size and SD wire CRC. Lattice RAM models passed 14593 checks;
legacy bus/disk tests passed 48/60949 checks. All 47 protected HC1200 source
hashes still match. Eleven host format/utility checks and nine profile checks
passed. Simulation backing SD files are opened read-only.

Full RT-11XM cold boot from unit 7 uses the real 50 Hz timer and full five-second
wait: 3657 checks, 669227126 clocks, 114330 DMA words and 14076704 mapped fetches.
`SHOW MEMORY` and directory reads passed. The small menu harness accelerates
only the timer by 100x and separately verifies the exact tick count.

`uj11-hc7000-serv-menu-rt11xm.img.gz` is that tested 32 MiB card image: one RK07
partition, RH unit 7, marked default, with the HC7000 XM/HGX utility kit.
It is an image of a newly prepared card, not a backup of the physical SD card.
`sd-image.json` records its checksums. See `../../docs/serv-storage.md` for the
format and host commands (`menu`, `boot`, `boot-off`, `menu-off`, `verify`).

The first eight menu cases preceded adding the separate wire-CRC case and
`boot-off` host command; the firmware, ROM, menu payload and RTL are identical.
The wire-CRC and host tests cover the final additions. `qualification.json`
hashes the saved evidence and code inputs. Release logs are globally ignored;
include them explicitly with `git add -f` when committing this candidate.
