# SERV storage: software disk controllers on partitioned SD

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
`IOP=storage` with microcoded FPP is rejected before building. The current source
uses **19 KiB shared RAM in nineteen EBRs**: 18.5 KiB for SERV code/data/stack
and 512 bytes reserved for the autonomous SD/DMA sector buffer. The former
standalone J11 boot-ROM and sector-buffer EBRs are now part of this memory.
The first 18 KiB support full-width 32-bit access; the last KiB uses two
16-bit accesses per word and contains 512 bytes of optional cold data plus
the sector buffer. Sector traffic only stalls SERV accesses to that final KiB.
All ordinary code, BSS and the stack stay in the fast banks. SERV installs the bootstrap into ordinary SRAM
before releasing J11, and repeats the copy on board reset. Guest `RESET` does
not reinstall it. Code, data and stack remain in EBR; PDP-11 keeps all 2 MiB SRAM.
This change has passed RTL and synthesis, but is not yet installed on the board;
the installed 2AAE still uses 12 KiB and a separate boot ROM.
See [19 KiB RAM, EBR allocation and validation](serv-19k.md), the intermediate
[shared RAM checkpoint](serv-shared-ram.md), and
[SERV bootstrap](serv-bootstrap.md).
Legacy SERV remains RV32I. The verified intermediate compaction saves 1520 code
bytes and three microstore EBRs; see
[resource and regression results](../history/releases/hc7000-serv-compact/README.md).

## Shared peripheral interface

The storage profile forwards peripheral bus cycles to SERV. The request contains
an I/O-page address, read/write direction, byte lanes and write data. The CPU
holds the transaction until firmware replies with a word or NXM. Register files,
READY/GO, byte-write merging, per-drive state, command generations, initialization
and interrupt selection are C code/data; adding a controller does not require a
new RTL register bank. Firmware also answers absent-device probes and implements
177504/177506 boot metadata, CCR, MEMERR, board identification and UNIBUS map registers.

Existing physical UART, KW11 clock, HG pins and boot SPI
retain their hardware interfaces. Main RAM, the CPU MMU and processor registers
remain on their existing CPU paths. The shared sector engine still moves bulk
SPI/SRAM data; SERV interprets disk commands and MSCP packets.

SERV polls the bus between operations and while waiting for SD/DMA. A PDP-11
I/O cycle therefore takes firmware service time; ordinary instruction execution
and RAM access continue independently of disk commands. The boot-time SD-label
CRC validation can hold a probe longer than normal register accesses.

`firmware/storage/controllers.c` owns all emulated registers. `main.c` provides
SD transport, RH/RL/XP commands and scheduling; `rk05.c` and `mscp.c` provide the
other command implementations, with declarations in `storage.h`. The initial
label buffer is reused for controller state and MSCP packet scratch after the
label is decoded. The linker reserves a 640-byte stack; stack usage is reported
by GCC, and its call paths must be checked when extending the firmware. The
current controller integration tests observe at most 352 bytes, including bus
service during DMA. The RTL guard checks every SERV data/stack write against
the ELF bounds. Storage startup initializes `gp` without relaxation; subsequent
global-data and constant-table references may use linker GP relaxation. Legacy
firmware retains its original flags and startup.

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
MMR3 bit 5 enables translation for RK11, RL11 and RH11 DMA; page 31 maps to the I/O
page. The 128-byte table and address translation live in SERV RAM; the former
hardware map EBR and per-word translation logic are removed. SERV splits DMA
bursts at 8 KiB UNIBUS page boundaries, translates each fragment, and submits
physical addresses to the shared sector engine. Inhibited addresses stay fixed;
compare mismatches survive subsequent fragments, and only the final LOAD
fragment may zero-pad its sector. DMA to the I/O page or outside installed SRAM
returns NXM without accessing aliased RAM. With mapping disabled, the existing
RK07 18-bit wrap and RL/RP direct DMA paths remain available. The CCR at 177746
retains software writes; 177744 reports no memory parity error (no cache/parity
hardware is fitted). These addresses let the 2.9BSD loader take its 11/70 path.

The storage profile reports **MAINT 177750 = 001045** (KDJ11-B, UNIBUS,
ROM boot, BPOK, no FP accelerator) and **HIT/MISS 177752 = 000010**. These
are the 11/84 identification values used by the local `pdp1184` reference and
[SIMH's J11 register implementation](https://github.com/simh/simh/blob/master/PDP11/pdp11_cpumod.c).
They describe the bus presented to the OS; they do not enable FPP in the CPU.
The legacy MMU profile retains its previous MAINT value.

This corrects the stock RT-11 RKX handler's Q-bus restriction: the previous
11/73A identity made RKX reject transfers above 64 KiB before issuing any RK
controller command. With the UNIBUS identity it uses 18-bit DMA and the map.
`RKX.SYS`, monitor binaries and compiler binaries remain unchanged. SD compiler
preparation modifies only the three startup command files to assign RK1/2/3
to BAS/PAS/FOR and preserves DM1 as VOL.

The [7AE8 release record](../history/releases/hc7000-serv-unibus/README.md) includes
source-matched integration tests and the 50 MHz synthesis: 3979 LUT4 and
25 EBR, saving 406 LUT4 and one EBR relative to 9DF6. Its compiler test
boots the remapped media, runs Pascal XM directly from RK2, and compiles,
links and executes programs with the stock drivers and FPP disabled.

RK11/RK05 is at **177400**, vector **220**, with eight units. It implements
read/write/write-check/read-check, seek, recalibrate and software write lock,
18-bit DMA wrapping and address inhibit. Short writes preserve the sector tail.

RQ/MSCP is at **172150**, with a programmable interrupt vector and four units.
SERV handles UQSSP initialization, command/response rings, credits, ONLINE,
GET UNIT STATUS, SET CONTROLLER/UNIT CHARACTERISTICS, AVAILABLE, ACCESS,
READ/WRITE/COMPARE and direct 22-bit packet/data DMA. Short writes zero-pad their
sector tail. Images are regular MSCP disk partitions; geometry is selected from
the supported RD/RA types using image capacity.

The table still reserves RK06, RH70 mode for RK06/07, and TK50 `.tap` entries.
Unsupported nonboot entries are ignored; an unsupported boot entry gives startup
error 7. Tape entries retain file length separately from capacity; the utility
does not parse tape records.

Kinds match `lsi11/demo/boot_menu.asm`: RK=1, RH=2, XP=3, RQ=4, RL=5, TQ=6.
`firmware/boot/SDMENU.asm` is loaded at PDP 0100000. It lists controllers and
present RK, RH, RL, XP and RQ units, rejects absent units, and dispatches the selected
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
backward compatibility, 1 means RK, 3 means XP, 4 means RQ, 5 means RL). Non-RH boot requires the menu. On failure the low bits hold an error code:
1 CMD0, 2 CMD8, 3 ACMD41, 4 OCR, 5 CSD/CRC, 6 both labels invalid,
7 unsupported boot media/mode, 8 missing boot entry or non-RH boot in direct-boot mode.
The ROM adds diagnostic 9 for an invalid menu descriptor/payload and 10 for
SD read or transport CRC failure (11 and 12 respectively in octal). The bootstrap saves diagnostics
at 157774/157776 and enters microcoded ODT on failure.

Read-only **177506** reports RK05 units in bits 15..8 and RQ units in bits
3..0. The menu uses these masks without resetting MSCP or changing drive state.

SERV uses one shared MMIO mailbox at hexadecimal **40000000**:

| Offset | Direction | Meaning |
| --- | --- | --- |
| 00 | read | Request pending (bit 0), bus reset (1), IRQ acknowledgement (2), SD ownership (3), acknowledged vector divided by four (14..8), live MMR3 UNIBUS-map enable (16) |
| 00 | write | Clear consumed reset/IRQ acknowledgement flags (bits 1/2) |
| 04 | read | Address (12..0), write (13), byte lanes (15..14), data (31..16) |
| 08 | write | Response data (15..0) and NXM (16); completes this CPU cycle once |
| 0C | write | Publish interrupt vector (8..2), valid (16) |
| 10 | write | Request SD ownership (0), UNIBUS DMA (1), cancel DMA (2), service active (3) |

SPI remains at 40000100, the sector/DMA engine at 40000200, and the millisecond
counter at 40000300. MSCP uses engine operations 6/7 for single-word physical DMA.
There are no per-controller SERV MMIO windows in this profile.

Each command has a software generation token. Clearing a controller invalidates
its old DMA/completion without resetting SERV or another controller. An active
SPI sector transaction finishes before ownership is released. PDP `RESET`
clears commands and IRQs but retains SD attachments and head positions. Board
reset restarts SERV and rereads the table. Independent interrupt requests queue
in SERV RAM; the bridge publishes one BR5 vector at a time, in order RL, RH, XP,
RK, RQ. An acknowledgement is retained until firmware consumes it and cannot be
overwritten by a simultaneous publication. Bus reset rejects a stale response.

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
The earlier `releases/hc7000-bsd` contains the SD archive and matching JED. Its RTL and
SIMH boots pass through multiuser root login, RL/RP file writes/readback and
`sync`; that release's RTL run has 18926 checks. Its 24 MHz MAP/PAR/TRACE build uses
5435 LUT4, 2137 FF and 25/26 EBR, with Fmax 25.539 MHz. The old partitioned
RH7 RT-11XM image also boots on that RTL. See its README and manifests for
exact checksums, source inputs and UART logs. The replacement shared-I/O
implementation and its qualification are preserved separately in
`releases/hc7000-serv-io`; it has not yet been installed on the physical board.

The additional [OS boot matrix](boot-validation.md) covers the original
five-disk RT-11 XM setup, RT-11 V4 on RK0, and both RSX RQ images. The software
controller build boots XM from RH0, reads RH1 and all three RK05 images, and
boots V4 from RK0. The requested RSX RQ0 image contains a nonbootable placeholder;
RQ1 is checked separately without changing the original disk ordering.

RSX-11M-PLUS also needs the CPU's CSM instruction during STARTUP. The MMU
microcode now builds its supervisor stack frame, changes mode and loads the
entry PC from supervisor I-space. Thirty-one added microinstructions fit in
the existing nine microstore EBRs. Instruction dispatch and supervisor-space
selection require a small RTL change: synthesis rises from 3985 to 4014 LUT
for the shared-I/O build, with no additional EBR. The final 24 MHz build uses
1187 flip-flops and 26/26 EBR and passes routed timing at 26.087 MHz.

The subsequent [integer/MMU corrections](j11-isa-audit.md) add TSTSET/WRTLCK
in ten existing microstore words, correct MFPI/MTPS/RESET, and hold off new
DMA grants while TSTSET executes its read/modify/write. Synthesis `isa-fix-style-02`
uses 4043 LUT, 1188 FF and the same 26 EBR; it passes 24 MHz constraints with
a routed Fmax of 25.662 MHz. SERV firmware remains byte-for-byte identical.
HC1200 and the MMU-less microcode are unchanged.

## Qualification

`tests/test_storage_label.py` cross-checks the actual C decoder against Python,
including every single-bit corruption, CRC-correct malformed labels, bounds,
copy recovery, disk import/export and exact tape byte lengths.
`tools/test_storage.py` runs actual SERV instructions, SPI pins, SRAM pins and
a concurrent CPU master. It checks independent drives, write lock, partition
bounds before writing, CRC, cancellation, DMA wrap and both label failure paths.
Backing images are opened read-only by the SD model; writes go to a RAM overlay.

The shared request/response bridge has an independent test in
`tests/mmu/tb_iop_bus.v`, including held cycles, NXM, reset and interrupt
acknowledgement races. `tools/test_storage_rk_rq.py` executes the real firmware
with separate writable/protected drives, RK05 short writes, UQSSP initialization,
two-slot rings, interrupts, 22-bit DMA, MSCP read/write/compare, error responses
and controller reset. The integration and OS tests also monitor SERV RAM writes
against linker boundaries and report measured stack usage.

An independent EBR check is available in `tests/tb_storage_ram.v`; compile with
`UJ11_IOP_VENDOR_RAM` and Diamond's PDPW8KC/DP8KC/GSR/PUR models. It tests all
3584 words, all byte lanes, bank isolation, consecutive reads, shared CPU/sector
collisions, padding bursts and reset. It verifies the sector's half-word view
against the same memory accessed by SERV, including both halves of each word.
`tools/test_storage_ram.py` also executes real RV32IC split fetches across the
private/shared-bank boundary at 24 and 50 MHz.
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

The subsequent CPUERR/PIRQ and kernel-stack changes use **4173 LUT, 1205 FF,
26/26 EBR**, with routed Fmax **26.263 MHz** at the same 24 MHz system clock.
These are processor functions; the SERV peripheral implementation and its
10515-byte RV32IC firmware are unchanged. The increase over the integer-fix
build is 130 LUT and 17 FF, with eleven additional microinstructions in the
existing EBR layout. See [CPU events](cpu-events.md) for behavior and validation.
