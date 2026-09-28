# SERV storage: partitioned SD, RK07, RL01/RL02 and RM05

This is an opt-in HC7000 **MMU** build. The default remains `IOP=legacy`.
HC1200 and the MMU-less HC7000 sources, firmware and boot path are unchanged.

```
make BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage hardware
make BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage test-storage
```

Direct Python tools use `UJ11_MMU_FPP=off UJ11_MMU_IOP=storage`.
The storage firmware uses RV32IC (compressed instructions) with the existing
RISC-V cross compiler on Linux; the builder
also accepts a source/hash-matched `build/hc7000-mmu-iop` cache on macOS.
`IOP=storage` with microcoded FPP is rejected before building: its 8 KiB SERV RAM
uses eight EBRs. Code, data and stack remain in EBR; PDP-11 keeps all 2 MiB SRAM.
Legacy SERV remains RV32I. The verified intermediate compaction saves 1520 code
bytes and three microstore EBRs; see
[resource and regression results](../releases/hc7000-serv-compact/README.md).

## Implemented and reserved

Implemented: one RK611/RH11 controller at 177440, vector 210, with up to eight
RK07 drives (units 0..7), each backed by an independent SD partition. Reads and
writes retain sector DMA, CRC16 checking, partial-sector zero padding and 18-bit
DMA wrap. A transfer crossing the end of a drive is rejected before its first
write. Write-protected media reports DS.WRL and ER.WLE (octal 004000).

RL11 is at **174400**, vector **160**, with four RL01/RL02 units. It implements
status, relative seek, three-word read-header FIFO, read, write, write-check and
read without header checking. Guest sectors are 256 bytes; partial writes zero
only the remainder of the selected RL sector, preserving the adjacent half of
the physical SD sector. Transfers stop at a track boundary with a residual count.
The BAE extension supports direct 22-bit DMA when UNIBUS mapping is disabled.

XP/RP is at **176700**, vector **254**, with eight RM05 units (823 cylinders,
19 heads, 32 sectors, 512 bytes per sector). It provides per-drive position,
attention/volume status, seek/recalibrate/preset/pack, read/write/write-check,
22-bit BAE and address-inhibit transfers. Short writes preserve their sector tail.
This is the RM05/RH70-style direct DMA path; it bypasses UNIBUS mapping.

In the storage profile, **170200..170376** exposes the 32 UNIBUS map registers.
MMR3 bit 5 enables translation for RL11 and RH11 DMA; page 31 maps to the I/O
page. The mapper uses one EBR, and DMA to anything outside installed SRAM
returns NXM without accessing aliased RAM. With mapping disabled, the existing
RK07 18-bit wrap and RL/RP direct DMA paths remain available. The CCR at 177746
retains software writes; 177744 reports no memory parity error (no cache/parity
hardware is fitted). These addresses let the 2.9BSD loader take its 11/70 path.

The table also reserves RK05, RK06, MSCP, RH70 mode for RK06/07, and TK50 `.tap`
entries. They are not implemented by SERV yet. Unsupported nonboot entries are
ignored; an unsupported boot entry gives startup error 7. Tape entries retain
file length separately from capacity; the utility does not parse tape records.

Kinds match `lsi11/demo/boot_menu.asm`: RK=1, RH=2, XP=3, RQ=4, RL=5, TQ=6.
`firmware/boot/SDMENU.asm` is loaded at PDP 0100000. It lists controllers and
present RH, RL and XP units, rejects absent units, and dispatches the selected
bootstrap. `-rp` in the emulator corresponds to the SD table's `xp`/`rm05`.

The `bootable` flag selects the **default for automatic boot after five seconds**.
Enter boots it immediately; another key cancels the timer and opens manual
selection. Without a bootable entry, the menu waits for a selection indefinitely.
The timeout polls 250 ticks of KW11-L at 50 Hz, with interrupts disabled; it does
not depend on instruction-count loops. Menu errors return to selection; a bad
menu image is rejected by the first-stage ROM and enters ODT.

## Host utility

Run from `uJ11-fpga`. All sizes and positions below are 512-byte SD sectors.
This example creates a new 64 MiB card image, imports an existing raw RK07 and
boots it as drive 7. Import/export preserve the image bytes without a filesystem
conversion.

```
python3 tools/sdcard.py build/storage-card.img init --blocks 131072
python3 tools/sdcard.py build/storage-card.img add rk07 7 --boot --label RT11XM
python3 tools/sdcard.py build/storage-card.img import rh 7 build/sd-hc7000/uj11-hc7000-rt11xm.img
python3 tools/sdcard.py build/storage-card.img add rk07 0 --label DATA
python3 tools/sdcard.py build/storage-card.img list
python3 tools/sdcard.py build/storage-card.img verify
python3 tools/sdcard.py build/storage-card.img export rh 7 build/exported-rk07.img
python3 tools/sdcard.py build/storage-card.img boot rh 0
make BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage storage-menu
python3 tools/sdcard.py build/storage-card.img menu build/storage-menu/menu.img
# Optional: disable automatic boot; always wait for a manual selection.
python3 tools/sdcard.py build/storage-card.img boot-off
# Optional: restore a default and remove the menu for immediate legacy-style boot.
python3 tools/sdcard.py build/storage-card.img boot rh 7
python3 tools/sdcard.py build/storage-card.img menu-off
```

`add` supports `--start`, `--blocks`, `--readonly`, and `--rh-mode rh70`.
Automatic placement finds a free interval on 2048-sector boundaries. Fixed
media sizes must match their geometry; variable-size MSCP/TK50 requires
`--blocks`. Controller numbers are fixed to 0 in v1. MSCP and RL have four unit
slots, the other families eight. At most one entry can be bootable.

Regular-file `init` and `export` refuse to overwrite existing files. Raw block
or character devices require the explicit global `--device` option, for example
`sdcard.py --device /dev/DEVICE init --blocks ACTUAL_SECTORS`. Use the actual
reported card capacity, unmount its filesystems first, and identify the device
before writing. Raw `init` overwrites sectors 0 and 1 and retains the data area.
`import` intentionally replaces an existing partition's payload, including a
partition marked guest-read-only. Payload replacement is not transactional;
keep the source image until copying has completed. Metadata is written after
the payload is flushed. No automatic repartitioning of the connected board's
SD card is performed by the build or test tools.

## Label v1

All multibyte fields are unsigned little endian. Sector 0 is authoritative;
sector 1 is its recovery copy. The header occupies 64 bytes, followed by up to
14 entries of 32 bytes. All unused entries and reserved bytes must be zero.

| Header offset | Bytes | Meaning |
|---|---:|---|
| 0 | 8 | Magic `UJ11SD\0\0` |
| 8 | 2 | Version 1 |
| 10 | 2 | Header size 64 |
| 12 | 2 | Entry size 32 |
| 14 | 2 | Entry count, 0..14 |
| 16 | 4 | Update sequence, wraps at 2^32 |
| 20 | 4 | IEEE CRC32 of all 512 bytes, with this field zero |
| 24 | 4 | Card size in SD sectors |
| 28 | 4 | Reserved prefix size; host default 2048 |
| 32 | 4 | Backup LBA, always 1 |
| 36 | 4 | Feature flags: bit 0 installs the SD menu; other bits zero |
| 40 | 24 | Reserved zero |

| Entry offset | Bytes | Meaning |
|---|---:|---|
| 0 | 1 | Controller kind |
| 1 | 1 | Controller instance, currently 0 |
| 2 | 1 | Unit number |
| 3 | 1 | Media ID |
| 4 | 2 | Flags: bit 0 default/autoboot, bit 1 guest read-only |
| 6 | 1 | Controller mode: RH11/default=0, RH70=1 |
| 7 | 1 | Reserved zero |
| 8 | 4 | Starting physical SD LBA |
| 12 | 4 | Allocated sectors |
| 16 | 8 | Image length in bytes; disks use the full allocation |
| 24 | 8 | Printable ASCII label, zero padded |

Media IDs: RK05=1, RK06=2, RK07=3, RM05=4, MSCP=5, RL01=6, RL02=7,
TK50=8. Fixed capacities, in SD sectors: 4872, 27126, 53790, 500384,
10240 and 20480 respectively. RL's guest sectors are 256 bytes; the table
still counts physical SD sectors of 512 bytes.

Both parsers reject bad CRC, unsupported versions/features, invalid units,
geometry mismatch, overlap, duplicate units, mixed modes for one controller,
multiple boot entries and out-of-card ranges. SERV obtains physical capacity
from SDHC CSD v2 and validates SD transport CRC16 before decoding either copy.
The host flushes the backup first, then the primary. A valid primary wins even
if the backup has a newer sequence; recovery only happens if the primary is
invalid. `verify` reports differing copies. This provides torn-label recovery,
not atomic payload replacement. Sector counts are limited to 32 bits in v1.

## Menu image

The descriptor occupies SD LBA 2; its payload starts at LBA 3 and contains
1..16 sectors. Menu installation requires at least 19 reserved sectors, even
for a shorter menu, so the fixed loader window can never overlap a partition.
The current menu is padded to six sectors; its exact size and hashes are recorded by the builder. Header fields are:

| Offset | Bytes | Meaning |
|---|---:|---|
| 0 | 8 | Magic `UJ11MENU` |
| 8 | 2 | Payload sector count, little endian |
| 10 | 2 | Entry/load address 0100000, little endian |
| 12 | 2 | CRC16-CCITT of the padded payload, little endian |
| 14 | 2 | Version 1, little endian |
| 16 | 494 | Zero |
| 510 | 2 | CRC16-CCITT of header bytes 0..509, **big endian** |

Both CRCs use polynomial 0x1021, initial value 0. The complete header has CRC
remainder zero. The host validates the envelope before writing; replacement
first disables the old menu flag, flushes the new image, then publishes the flag.
An interrupted update cannot select a partially copied menu. `verify` also
checks an installed menu. `menu-off` preserves the payload but clears its flag.
Old storage firmware does not understand this feature and rejects the label;
install the matching firmware before using a card with the menu flag set.

## Startup and reset

SERV owns SPI at reset, initializes SDHC/SDXC, reads CSD, validates the label,
and publishes drive presence and the default unit. The 430-byte PDP bootstrap
polls status. With feature bit 0 clear, it retains direct two-sector RK07 boot.
With the menu installed, it reads the descriptor and menu through the SD byte
port, checks all SD transport CRC16s and the stored descriptor/payload CRC16s,
and jumps into the menu. No extra EBR is used. The RH boot entry retains the
traditional `R0=unit, R1=177440` ABI. No guest SRAM is touched
by metadata parsing. `IOP=storage` requires a label; it deliberately does not
reinterpret a corrupt label as a raw disk. Existing unpartitioned cards use
`IOP=legacy` and the unchanged `SDBASE.MAC` loader.

Read-only PDP register **177504** is present only in the storage build:
bit 15 means startup finished, bit 14 means failure, bit 13 requests the menu,
and bit 12 means no default is configured. On success bits 2..0 contain the
default unit when bit 12 is clear; bits 5..3 select its kind (0 means RH for
backward compatibility, 3 means XP, 5 means RL). RL/XP boot requires the menu. On failure the low bits hold an error code:
1 CMD0, 2 CMD8, 3 ACMD41, 4 OCR, 5 CSD/CRC, 6 both labels invalid,
7 unsupported boot media/mode, 8 missing boot entry or non-RH boot in direct-boot mode.
The ROM adds diagnostic 9 for an invalid menu descriptor/payload and 10 for
SD read or transport CRC failure (11 and 12 respectively in octal). The bootstrap saves diagnostics
at 157774/157776 and enters microcoded ODT on failure.

SERV MMIO windows are RK=40000000, RL=40000400 and XP=40000500 (hex).
RK/RL status and completion indices are 16/17, epoch/claim indices 24/25;
XP uses 32/33 and 40/41. RK indices 18/19 publish present/protected masks and
20 publishes startup status; RL presence uses 18, XP uses 34/35.

Each command has a generation token. Clearing a controller invalidates its
old DMA and completion without resetting SERV or another controller. An active
SPI sector transaction finishes before ownership is released. PDP `RESET`
clears all controller commands and IRQs but retains SD attachments and head
positions. Board reset restarts SERV and rereads the table. Simultaneous disk
interrupts have independent vector acknowledgements: RL160, RH210 and XP254.

## 2.9BSD image

```
make BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage bsd-image
make BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage test-storage-controllers
make BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage test-bsd
```

`tools/build_sd_bsd.py` creates a new 512 MiB regular file from
`lsi11/disks/bsd2.9/`: RL0=`2.9BSD-root.rl02` (default), RL1=`swap.rl02`,
XP0=`2.9BSD-usr.rm05`. It copies every source byte unchanged and zero-fills
the omitted tails up to physical geometry, verifies all three payloads,
installs the menu, and produces a deterministic gzip plus manifest/checksums.
It never opens a physical card. The source filesystem already has `/etc/fstab`
entries for root, `/usr` on `/dev/xp0h`, and swap on `/dev/rl1`.

At `70Boot`, enter `rl(0,0)rlunix`; at the single-user `#`, press Ctrl+D,
then log in as `root`. The board has 2 MiB, not the 4 MiB of some SIMH examples.
The original `/etc/ttys` is retained for future display/keyboard terminals;
until DZ is implemented, init reports that tty00..tty07 cannot open. Wait about
two seconds at `login:` before typing, as this getty clears early input after
its initial delay.
The new BSD/UNIBUS build still needs its own physical-board qualification;
the earlier RH7-only hardware release remains separately preserved.
`releases/hc7000-bsd` contains the SD archive and matching JED. Full RTL and
SIMH boots pass through multiuser root login, RL/RP file writes/readback and
`sync`; the final RTL run has 18926 checks. The 24 MHz MAP/PAR/TRACE build uses
5435 LUT4, 2137 FF and 25/26 EBR, with Fmax 25.539 MHz. The old partitioned
RH7 RT-11XM image also boots on this RTL. See the release README and manifests
for exact checksums, source inputs and UART logs.

The additional [OS boot matrix](boot-validation.md) covers the original
five-disk RT-11 XM setup, RT-11 V4 on RK0, and RSX on RQ0. XM boots from RH0
and reads RH1 on the current RTL; RK05 and RQ/MSCP remain unsupported. The
requested RSX RQ0 image is also nonbootable in SIMH, while its RQ1 companion
boots RSX-11M-PLUS. Saved tests preserve these failures as explicit limitations.

## Qualification

`tests/test_storage_label.py` cross-checks the actual C decoder against Python,
including every single-bit corruption, CRC-correct malformed labels, bounds,
copy recovery, disk import/export and exact tape byte lengths.
`tools/test_storage.py` runs actual SERV instructions, SPI pins, SRAM pins and
a concurrent CPU master. It checks independent drives, write lock, partition
bounds before writing, CRC, cancellation, DMA wrap and both label failure paths.
Backing images are opened read-only by the SD model; writes go to a RAM overlay.

An independent EBR check is available in `tests/tb_storage_ram.v`; compile with
`UJ11_IOP_VENDOR_RAM` and Diamond's PDPW8KC/DP8KC/GSR/PUR models. It tests all
2048 words, all byte lanes, bank isolation and consecutive bank selection.
For full OS boot, pass a partitioned card image:

```
make BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage test-rt11 MMU_SD_IMAGE=build/storage-card.img MMU_BOOT_MENU=1
```

`test-storage-menu` exercises timeout, Enter, cancellation, choosing a different
unit, absent controller/unit rejection, no-default operation, direct boot without
a menu, and corrupt header/payload/size. Its KW11 tick is accelerated by 100x;
CPU/SPI/UART clocks remain unchanged and it checks exactly 250 timeout ticks.
Full RT-11 boot uses the real 50 Hz timer and the full five-second delay.

The earlier RH7-only menu MAP/PAR/TRACE run uses 3366 LUT, 1103 FF, 24/26 EBR; Fmax 25.713 MHz
at the unchanged 24 MHz system clock (12 MHz external oscillator). Physical
qualification on HC7000 now passes with one RK07 partition at RH7: FLASH verify,
RT-11XM with 2 MiB and 22-bit MMU, 4.987-second automatic boot, immediate Enter,
timer cancellation, rejection of absent devices, and manual RH7 boot. SD copying
passes a 116-sector binary comparison; HGX passes a 34-sector host/SD round trip
and HGTIME synchronization. SETF confirms that FPP remains disabled.

Evidence and the original full-card backup reference are in
`releases/hc7000-serv-menu/hardware/`. Multiple-drive, no-default and malformed
media cases have been tested in simulation, not on the physical board yet.
`tools/hardware_storage_menu.py --case auto|enter|cancel --out NEW_DIR` arms a
UART capture and waits for the operator's long RESET, using the RH7-only test
card. `tools/hardware_storage_hg.py --unit 7 --hgfsd PATH --out NEW_DIR` checks
the already installed XM HG driver, time synchronization and a binary round
trip. Both tools require a fresh output directory and restore a UART reader
specified with `--pause-pid`, when one is present.
