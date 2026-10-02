# HC7000: UNIBUS identification and software map in SERV

> Историческая запись: `releases/hc7000-serv-unibus/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


JED **7AE8**, 50 MHz from the 12 MHz oscillator; FPP off, FIS retained,
2 MiB SRAM, hardware diagnostic display enabled. Compiler qualification,
controller integration and the complete RTL OS regression pass.
**Installed on the physical HC7000 on 29 September 2026**, replacing 9DF6.
FLASH Erase/Program/Verify completed successfully; the compiler suite also
passes on the board with the existing SD and stock drivers.

Stock RT-11 RKX refuses DMA above 64 KiB when the monitor reports Q-bus.
The former MAINT value identified the board as 11/73A, despite the existing
UNIBUS map. The storage profile now reports MAINT `001045` and HIT/MISS
`000010`, identifying a J11/UNIBUS system as 11/84. This follows the local
`pdp1184` and SIMH register model. No RKX, monitor or compiler patch is needed.

The map table, CSR byte writes, reset and DMA translation now run in SERV.
The FPGA retains only the physical sector engine and exposes MMR3.BME in
the SERV mailbox. Mapped transfers split at 8 KiB boundaries; XP/RQ retain
22-bit direct DMA. Page 31 and uninstalled SRAM return NXM without aliasing.

The routed design uses **3979 LUT4, 1342 FF, 25 EBR**, compared with
4385/1511/26 for 9DF6: **406 LUT4, 169 FF and one EBR freed**.
Fmax is **50.467 MHz**; the 50 MHz clock and external SRAM constraints pass.
`synthesis/` contains the reports, source hashes and JED export record.
`sources/` captures those exact inputs, including generated microcode and
SERV RAM. No CPU microcode or HC1200/MMU-less board source changed.

SERV still has 12 KiB RAM in twelve EBRs: 10647 bytes of code/constants,
976 bytes of BSS, a 24-byte gap and 640 bytes reserved for the stack.
GP relaxation saves 188 code bytes compared with the first software-map
build. Startup sets GP before any relaxable address. The longest observed
stack use in controller integration is 352 bytes; the static call chain
main -> sector_io -> fetch_sector -> transfer -> poll_io -> attention also
sums to 352 in GCC's frame report. There is no recursion or interrupt stack.
The RTL write guard checks the actual ELF bounds throughout each test.

Completed integration checks in `validation/` cover all five controllers,
SD label fallback/errors, all 32 map entries and byte masks, reset, mapped
read/write/compare across noncontiguous pages, address inhibit, 22-bit map
addition wrap, NXM protection and RQ bypass while BME is enabled.
The legacy RV32I firmware is byte-identical to its released 1188-byte image.

The compiler qualification boots RH0 with the remapped SD layout and stock
RKX/DMX drivers. BASIC computes `2+3 = 5`; Pascal XM is run directly from
RK2, compiles ADDER, links LIBEIS and prints `5.000000E+00` for `2 3`;
FORTRAN from RK3 compiles, links FORLIB and prints `FORTRAN OK`.
This is the actual CPU/SERV/SD/SRAM/UART RTL, with a source-matched firmware
image, FPP disabled, 2 MiB RAM and a stack write guard. The private system
image differs from the distribution in only nine startup assignment bytes.
The backing SD image and all source disks retain their hashes after testing.

To repeat from `uJ11-fpga` (with `lsi11/rt11tool` built):

```sh
python3 tools/build_storage_menu.py --out build/unibus-menu-new
UJ11_MMU_IOP=storage UJ11_MMU_FPP=off UJ11_MMU_CLOCK_MHZ=50 \
UJ11_HC7000_DIAGNOSTICS=1 python3 tools/test_sd_compilers_rtl.py \
  --menu build/unibus-menu-new/menu.img --out build/unibus-compilers-new
```

RTL OS regression also passes RT-11 XM directories, RT-11 V4 system files,
and 2.9BSD multiuser login. BSD reads `/usr` and `/etc/fstab`, writes and
reads test files on both RL root and XP `/usr`, removes them and runs `sync`.
The source images remain unchanged; simulated guest writes use a RAM overlay.

RSX-11M-PLUS V4.6 BL87 boots from RQ1, completes STARTUP, accepts the date,
reports DU0/DU1 and lists `DU1:[1,54]RSX11M.SYS;1` (1026 blocks).
The separate RQ0 check reproduces the distribution's nonbootable placeholder
and HALT PC `000034`; it is an expected nonbootable case, not an OS boot.
`validation/boot-matrix.json` records all four matrix cases and unchanged
source/SD hashes. Individual directories contain UART and simulation logs.

| RTL qualification | Checks | Clocks | DMA words |
|---|---:|---:|---:|
| BASIC, Pascal XM, FORTRAN | 5062 | 791475523 | 674648 |
| RT-11 XM | 6107 | 597670577 | 164871 |
| RT-11 V4 | 2816 | 330195047 | 63670 |
| 2.9BSD | 24033 | 1758958563 | 419072 |
| RSX-11M-PLUS, RQ1 | 3346 | 2166422333 | 786147 |

The JED SHA-256 is
`dbf2190d161a168611bcee6e8f0903b4dbb2dffab97cb380bfeb3dd87dba6a1b`.
`SHA256SUMS` covers the packaged firmware, reports, logs and source snapshots.

The [hardware session](validation/hardware/session.json) and
[UART log](validation/hardware/uart.txt) record ODT confirmation, installation,
RH0 boot, all four compiler-volume directory reads and the complete tests:
BASIC `PRINT 2+3` returns 5; `RUN PAS:XM` starts directly from RK2, compiles
ADDER and links LIBEIS, then prints `5.000000E+00` for `2 3`; FORTRAN compiles
and links with FORLIB, prints `FORTRAN OK` and exits with `STOP --`.
No `Ovly err` occurs. Only the existing CTPAS/CTFORT test outputs on VOL were
rewritten; the system and compiler drivers were not patched. The SD layout
and default RL0 boot are unchanged. The board is left at the RT-11 XM prompt,
and the host UART is released. The other OS results above remain RTL tests;
this hardware session selected RH0 only.

`SHOW CONFIGURATION` prints 11/84, 2048 KB and FIS. Its inherited strings
`Floating Point Microcode`, `FPU support` and `60 Cycle System Clock` do not
prove an installed FP unit or measure the timer. This image has FPP disabled
(the compiler test asserts the actual RTL signal) and a 50 Hz hardware tick.
The no-FPP/FIS SIMH 11/40 reference is retained in the separate compiler-media
release; it is not presented as execution of the HC7000 or a J11 CPU.
