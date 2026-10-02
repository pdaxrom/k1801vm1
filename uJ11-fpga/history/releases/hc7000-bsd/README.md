# HC7000: 2.9BSD on RL02 + RM05

> Историческая запись: `releases/hc7000-bsd/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


Build: `BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage`.
Target: LCMXO2-7000HC-4TG144C, 12 MHz oscillator, 24 MHz CPU,
2 MiB IS61WV102416BLL-10T SRAM. Installed on the physical board on 2026-09-28:
FLASH Erase/Program/Verify passed, and the 512 MiB BSD image was written to
the SD card with full SHA-256 readback verification. Physical BSD boot
qualification is pending the user's check. See `hardware/installation.json`;
the prior RT-11 card's overwritten 512 MiB are backed up on the Linux bench.

`uj11-hc7000-2.9bsd.img.gz` expands to a **512 MiB** SD image.
It contains the partition table, timed SD boot menu and these disks:

| Guest drive | Source image | SD start (512-byte sectors) | Size (sectors) |
|---|---|---:|---:|
| RL0, default boot | `lsi11/disks/bsd2.9/2.9BSD-root.rl02` | 2048 | 20480 |
| RL1, swap | `lsi11/disks/bsd2.9/swap.rl02` | 22528 | 20480 |
| XP0 / RP0, `/usr` | `lsi11/disks/bsd2.9/2.9BSD-usr.rm05` | 43008 | 500384 |

All source bytes are preserved. The input files omit the last 7680, 7680 and
11264 bytes respectively; those geometry tails are zero-filled. The original
files are unchanged. `sd-image.json` records the original, raw-image and gzip
hashes and the partition/menu verification. The existing `/etc/fstab` already
selects `/dev/rl0`, `/dev/rl1` swap and `/dev/xp0h` for `/usr`.

## Boot

Use the matching JED in `synthesis/impl1/bsd-rl-xp-04_impl1.jed` and the
uncompressed image on an SD card. The menu defaults to RL0 after five seconds;
Enter boots immediately. At `70Boot` enter:

```
rl(0,0)rlunix
```

The kernel reports `mem = 1979072` with this board's 2 MiB SRAM. It attaches
XP at CSR 176700/vector 254 and RL at CSR 174400/vector 160. At the single-user
`#`, press **Ctrl+D** to start multiuser mode, mount `/usr`, and obtain the
`login:` prompt; log in as **root**. DZ, LP and TM are absent on this board and
are skipped during autoconfiguration. `/etc/ttys` is intentionally unchanged:
`init` reports that tty00..tty07 cannot open until the additional terminals
are implemented. Allow about two seconds after `login:` before typing; this
version of getty delays and flushes early input after printing its prompt.
UART remains 115200/8/N/1. BSD's KL driver adds software parity in data bit 7;
the host terminal must strip that bit. For an already running picocom, run
`stty -F /dev/ttyUSB1 istrip` from another host shell. Repeat after restarting
picocom, which resets the serial termios flags. Keep the wire format at 8/N/1.
The readable RTL UART log masks bit 7, as does SIMH's `SET TTO 7B`; it does not
represent an unfiltered UTF-8 terminal. DEL (0x7f) bytes after newlines are
padding emitted by the original loader/kernel for old terminals.
Automated input in the BSD test is paced at roughly
20 ms per character to allow the DL11's single receive register to be serviced;
the test also rejects receiver overrun.
This image retains its original startup
date; no BSD host-time synchronization service is installed.

## Hardware and firmware

- RL11: four RL01/RL02 units; status, seek, read-header, read/write/write-check.
- XP/RP: eight RM05 units; position and attention state, read/write/write-check,
  22-bit address extension and address-inhibit mode.
- Existing RH11/RK07 and partitioned RT-11XM support remain available.
- 32 UNIBUS map registers translate RL/RH DMA when MMR3 bit 5 is set.
  XP uses its direct 22-bit DMA path. Out-of-SRAM DMA reports NXM.
- Independent controller IRQs, command cancellation and reset generations.
  A PDP RESET preserves attached media and head positions; a board reset
  restarts SERV and rereads the SD table.
- A CPU fix clears the explicit-PSW instruction marker when entering a trap:
  an IRQ accepted immediately after writing PSW must load the vector's PSW.
  The new CPU regression fails before this fix and passes afterward.
- XP interrupt acknowledgment is retained when a handler rewrites an already
  enabled IE. Clearing attention updates CS1.SC, including byte-write handling.

SERV has 6320 bytes of RV32I code and 796 bytes of BSS in its 8 KiB EBR RAM;
the linker reserves at least 1 KiB for the stack. FPP remains disabled.
MAP/PAR/TRACE passes all 24 MHz clock and external SRAM constraints:
**5435/6864 LUT4, 2137 FF, 25/26 EBR, Fmax 25.539 MHz**.
The exported JED checksum is **A098**. `synthesis/jed.json` records its SHA-256
and source revision hash; local inputs match the Linux synthesis inputs.

## Validation

Full RTL cold boot uses the actual CPU, SERV firmware, 2 MiB SRAM pins, SD SPI
protocol, 24 MHz clock, 50 Hz timer and UART waveforms. It passes `70Boot` →
`rl(0,0)rlunix` → single-user shell → Ctrl+D → `/usr` mount → root login.
`ls /usr`, `/etc/fstab`, file creation/readback on both `/tmp` (RL0) and
`/usr/tmp` (XP0), cleanup and `sync` pass. The backing SD file is unchanged;
all simulation writes use a RAM overlay. Result: **18926 checks, 1135577869
clocks, 421120 DMA words, 19631824 mapped instruction fetches**.
See `validation/bsd/uart.txt` and its source-hashed result.

The same multiuser and file tests pass in SIMH with an 11/70, 2 MiB and no FPP,
using private copies of the three disks. The original images remain unchanged.
The RTL trace includes expected absent-device probes, memory-boundary probes,
user stack-growth MMU faults and SETD probes with FPP disabled; these are
handled by BSD and do not enter ODT.

Other saved checks:

- RL/XP integration: 54704 checks and 3349 DMA words, both generic and Lattice
  EBR models; 22-bit addressing, partial sectors, write-check, write protection,
  cancellation, simultaneous requests and independent IRQs.
- UNIBUS map: 95 checks each with generic and Lattice RAM, including byte writes,
  carry, 4 MiB wrap, hardwired I/O page and 2 MiB SRAM bounds.
- Final board CSR/IRQ/reset integration: 15 checks.
- Existing partitioned RH7 RT-11XM: 3657 checks, 355341321 clocks, 114330 DMA
  words; five-second default boot, memory and directory commands pass.
- Legacy MMU SERV profile: 60949 disk checks and 48 bus checks.
- CPU/MMU regressions with FPP disabled, enabled and Lattice ROMs; the new
  PSW/IRQ regression's original failure and corrected pass are preserved.
- Original menu cases, RL1/XP7 automatic boot, host label/menu validation,
  eleven profile checks and all 47 protected HC1200 source hashes.

`qualification.json` binds the packaged evidence and current source inputs;
`SHA256SUMS` covers every packaged file. These are simulation and synthesis
results; the new BSD build still needs physical-board qualification.

Additional RT-11 XM, RT-11 V4 and RSX boot validation is saved in
`validation/boot-matrix/`; see [the boot matrix](../../../docs/boot-validation.md).
The requested XM image boots from RH0 and reads RH1 on this RTL, but RK05
disks remain unavailable. RK0 and RQ0 boot selections are rejected with startup
error 7 because RK05 and RQ/MSCP are not implemented. These rejections are
recorded as limitations, not OS boot passes. Both RT-11 configurations pass
in SIMH on 11/73 and on 11/70 without FPP. The requested RSX RQ0 image has a
nonbootable placeholder block on both CPUs; the separate RQ1 control boots
RSX-11M-PLUS V4.6 BL87. The full requested matrix has not passed on FPGA.

## Rebuild

From `uJ11-fpga`, with the existing RISC-V cross compiler available:

```
make BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage hardware
make BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage bsd-image
make BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage test-storage-controllers
make BOARD=hc7000-lcd-sram CPU=mmu FPP=off IOP=storage test-bsd
```

The image builder requires a new output directory; choose another `BSD_OUT`
for repeated builds. It writes regular files only. The pinned `microasm11`
revision `e8b3ca8` reproduces the included menu binary byte-for-byte.
See `../../docs/serv-storage.md` for the label format and SD utility.

HC1200 and MMU-less source hashes remain unchanged. `IOP=legacy` remains the
default. Release logs are ignored globally by Git and need explicit inclusion
when this candidate is committed.
