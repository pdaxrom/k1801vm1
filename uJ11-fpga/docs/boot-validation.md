# HC7000 storage boot validation

## Installed board and SD, 2 October 2026

The current JED is **7CBA**, MMU / FPP off / FIS / SERV storage /
PAL terminal / PS/2, CPU/PAL 50/64 MHz. Physical boots of all four systems
below passed in the [7CBA installation record](../releases/hc7000-ps2-idle/README.md).
The terminal reserves 112 KiB SRAM; the guest sees 1936 KiB.
The [complete distribution SD image](../releases/sd-hc7000-multi/README.md)
now packages all 11 partitions, the boot menu, corrected XM assignments and
HG utilities. It starts from clean source disks; it is not a backup of the
mutable physical card. Its own extracted-disk boot checks are linked there.

| System | Boot menu controller | Unit | Other installed media |
|---|---|---:|---|
| 2.9BSD, default after five seconds | **5 — RL11** | **0** | RL1 swap, XP0 `/usr` |
| RT-11 XM V5.03 | **2 — RH11/HK** | **0** | RH1 storage, RK1 BASIC, RK2 Pascal, RK3 FORTRAN |
| RT-11 SJ V04.00C | **1 — RK11/RK05** | **0** | — |
| RSX-11M-PLUS V4.6 BL87 | **4 — RQ/MSCP** | **1** | RQ0 is present but not hardware bootable |

Cancel the countdown with any key except Enter, then select the controller
and unit. Enter during the countdown boots the default RL0 immediately.
RT-11 XM assigns `VOL:` to DM1, `BAS:` to RK1, `PAS:` to RK2 and `FOR:` to RK3.
The mapping differs from the isolated test matrix below because RK0 on the
physical card is reserved for RT-11 V4. Pascal without FPP needs freshly
compiled code linked with LIBEIS; the original FPP ADDER executable is not
a valid check of this configuration. [ADDER/KED diagnosis](hc7000-terminal.md#rt-11-xm-ked-после-аварийного-pascal).

## Original isolated test matrix

The requested configurations use the original files in `lsi11/disks`:

| Case | Attachments | Requested boot |
|---|---|---|
| RT-11 XM V5.03 | RH0 `rt11v5.3/system.dsk`, RH1 `rt11v5.3/storage.dsk`, RK0 `basic.dsk`, RK1 `pascal.dsk`, RK2 `fortran.dsk` (last three also under `rt11v5.3`) | RH0 |
| RSX | RQ0 `rsx11/rsxm11sys.dsk`, RQ1 `rsx11/rsx11mpbl87.dsk` | RQ0 |
| RT-11 V4 | RK0 `rt11v400.dsk` | RK0 |

SIMH uses private copies; RTL uses fresh partitioned SD files with a read-only
backing file and a RAM write overlay. Source hashes are checked after each run.
No physical board or SD card is modified by these tests.

## Physical SD: RT-11 V4 alongside BSD and RSX

On 29 September 2026, the bench SD gained RK0 containing the unchanged
`rt11v400.dsk` at LBA 1673216 (4872 sectors). All five existing BSD/RSX
partitions and the menu were preserved and checked by SHA-256; default boot
remains RL0. To select V4, cancel the five-second countdown with any key
except Enter, choose controller **1 (RK11/RK05)**, then unit **0**.

The physical HC7000 running JED `9DF6` booted `RT-11SJ V04.00C` from RK0,
listed its three monitor files with `DIR RK0:RT11*.SYS`, and read
`V4USER.TXT`. The board was left at the RT-11 prompt. The
[installation record and UART logs](../history/releases/hc7000-sd-rt11v4/README.md)
include the complete six-partition layout, backups, verification hashes,
and separate records of the initial menu-test script errors.

## Physical SD: RT-11 XM compiler suite

The same card now also has RH0/RH1 for the V5.03 system and storage disks,
plus RK1/RK2/RK3 for BASIC, Pascal and FORTRAN. RK0 remains RT-11 V4.
The copied system disk assigns `VOL:` to DM1, `BAS:` to RK1, `PAS:` to RK2
and `FOR:` to RK3 in STARTX, STARTF and STARTS. Cancel the boot countdown,
choose controller **2 (RH11/HK)** and unit **0**; default boot remains RL0.

All five images passed physical SD readback, and all six prior partitions
retained their hashes. SIMH without FPP passed the remapped XM boot, BASIC
arithmetic, Pascal compilation/link/execution with LIBEIS, and a FORTRAN
compile/link/run. A separate complete SIMH 11/40 run with FIS/MMU and no FPP
also passed all three languages.

On the physical HC7000 with the former JED `9DF6`, RH0 boot, all volume directory reads, BASIC and
FORTRAN compile/link/run pass. Pascal XM fails directly from RK2 with
`Ovly err 001070`, but the same executable copied to RH1 compiles and runs
the test with LIBEIS. The stock RKX driver rejects DMA beyond 64 KiB on
Q-bus before starting the controller; setting only RT-11's QBUS flag in
SIMH 11/40 reproduces the failure. The SD retains the stock driver.
See the [layout, evidence and installation record](../history/releases/hc7000-sd-compilers/README.md).

The subsequent JED `7AE8` identifies the storage profile as 11/84/UNIBUS
through SERV. The UNIBUS map table, CSR handling and DMA translation also
move from FPGA logic into SERV. The full CPU/SERV/SD/SRAM RTL now passes
direct Pascal XM execution from RK2, compilation of ADDER, linking with
LIBEIS and the result `5.000000E+00` for `2 3`. BASIC and FORTRAN also pass
with FPP disabled and FIS retained. No RKX or monitor patch is used.
This 50 MHz build frees 406 LUT and one EBR compared with 9DF6. The
[UNIBUS release record](../history/releases/hc7000-serv-unibus/README.md) contains
the source-matched synthesis and regression results and the installation status.

The complete 50 MHz RTL regression for 7AE8 passes: XM (6107 checks), V4
(2816), 2.9BSD multiuser and RL/XP file I/O (24033), and RSX RQ1 (3346).
RSX completes STARTUP, reports DU0/DU1 and lists the 1026-block
`DU1:[1,54]RSX11M.SYS`. RQ0 retains its expected nonbootable result.
These are RTL results. A subsequent physical session installed and verified
7AE8, booted RH0 and passed BASIC, direct RK2 Pascal XM compile/link/run with
LIBEIS, and FORTRAN compile/link/run with FORLIB. The stock disk drivers remain
unchanged. The board was left at the RT-11 XM prompt with the UART released;
the hardware session did not repeat the other OS boots. Its UART and programmer
logs are included in the UNIBUS release record above.

## Reference results

Both SIMH 11/73 and SIMH 11/70 boot RT-11 XM V5.03 and RT-11 SJ V04.00C.
XM reports 2 MiB and 22-bit addressing, reads RH1's ADDER files, and reads
the BASIC, Pascal XM and FORTRAN directory entries on all three RK05 disks.
V4 lists its system files and reads `V4USER.TXT`.

The exact RSX request **does not boot**. `rsxm11sys.dsk` contains a placeholder
boot block that prints `THIS VOLUME DOES NOT CONTAIN A HARDWARE BOOTABLE SYSTEM`
and halts at PC 000034. This is reproduced on both reference CPU models.
Without swapping or changing the disk images, a separate **RQ1 control run**
boots **RSX-11M-PLUS V4.6 BL87**, accepts the startup date, reports both DU0 and
DU1, and lists `DU1:[1,54]RSX11M.SYS`. It does not turn the RQ0 result into a pass.

All reference runs use 2 MiB and a 50 Hz clock. The installed SIMH 3.12 fixes
FPP capability for the 11/73 model and rejects `SET CPU NOFPP`; the separate
11/70 runs explicitly disable FPP. Neither reference CPU is presented as a
substitute for executing the HC7000 RTL.

## Initial 24 MHz FPGA qualification

| Configuration | Result on HC7000 MMU / FPP off / SERV storage |
|---|---|
| RT-11 XM, boot RH0 | Boot passes at 24 MHz with 2 MiB and 22-bit addressing; RH1 and all three RK05 directory reads pass. |
| RSX, boot RQ0 | The software MSCP controller loads the original placeholder. It prints its nonbootable-volume message and halts at PC 000034, matching SIMH. No guest SD writes occur. |
| RSX, boot RQ1 | RSX-11M-PLUS V4.6 BL87 completes STARTUP, accepts the date, reports DU0/DU1 and lists `DU1:[1,54]RSX11M.SYS` (1026 blocks). |
| RT-11 V4, boot RK0 | Boot and system-directory/text reads pass through the software RK05 controller. |

The XM RTL run has 6085 checks, 446981194 clocks and 164871 DMA words. It
executes the actual CPU, SERV firmware, SPI SD model, SRAM pins and UART
waveforms, including the five-second menu default. V4 passes 2816 checks in
187814328 clocks with 63670 DMA words. RQ0 remains an expected nonbootable image,
not a successful OS boot. Its own final RESET discards pending UART characters;
the test models this reset while checking the full message and HALT address.

RQ1 passes 3346 checks in 1637690453 clocks with 786149 DMA words. This run
includes the MMU CSM implementation needed by AT.T0. The terminal test waits
half a second after the date prompt so AT can queue its read, then waits for
STARTUP's final `QUE BAP0:/BATCH` before issuing interactive commands. Without
that delay, the immediately injected date was consumed as an MCR command.
The same FPGA/firmware sources pass with this console sequencing correction.

## Reproduce

From `uJ11-fpga`, use a new output directory for each run:

```
python3 tools/test_os_boot_simh.py --case rt11xm --cpu 11/73 --out build/matrix-xm-73
python3 tools/test_os_boot_simh.py --case rt11v4 --cpu 11/73 --out build/matrix-v4-73
python3 tools/test_os_boot_simh.py --case rsx --cpu 11/73 --out build/matrix-rsx-rq0-73
python3 tools/test_os_boot_simh.py --case rsx --cpu 11/73 --rsx-boot-unit 1 --out build/matrix-rsx-rq1-73
```

Repeat with `--cpu 11/70` to test without FPP. The nonbootable RQ0 run saves
`passed: false`, `status: not_bootable` and exits with status 2.

```
UJ11_MMU_FPP=off UJ11_MMU_IOP=storage python3 tools/test_os_boot_rtl.py --out build/matrix-rtl --menu build/storage-menu/menu.img
```

The RTL runner uses the supplied SD menu and current hardware/firmware.
It distinguishes a successful OS boot (`boot_passed`) from validation of the
nonbootable RQ0 placeholder (`expected_nonbootable`). Original SIMH evidence is
under `releases/hc7000-bsd/validation/boot-matrix/`; shared-I/O RTL
evidence for that stage is under `releases/hc7000-serv-io/validation/`.
These complete historical paths are preserved in the local Git archive `31549d1`;
[recovery instructions](../history/releases/README.md).

The MMU integer corrections are qualified again in
`releases/hc7000-isa-fix/validation/`: XM, V4 and both RQ cases retain the
results above. The accompanying 2.9BSD run passes 22108 checks, reaches
multiuser root, and verifies RL/RP file writes, readback and sync. These are
RTL results; at that qualification stage the JED had not yet been programmed
onto the board.

The subsequent C/header formatting preserves all SERV firmware bytes and
generated memory contents used by these runs. `serv-format.json` records
that comparison; `style-extra/` records the same check for both legacy IOP
profiles and the passing HG host tests.

## CPU-event regression

The CPUERR/PIRQ and fixed-limit stack implementation is qualified separately
in [`releases/hc7000-cpu-events/`](../history/releases/hc7000-cpu-events/README.md).
These runs use the current processor RTL and the unchanged SERV firmware:

| Case | Checks | Clocks | DMA words | Result |
|---|---:|---:|---:|---|
| RT-11 XM, RH0 | 6085 | 446981194 | 164871 | Boot; RH1 and RK0/RK1/RK2 directory reads |
| RT-11 V4, RK0 | 2816 | 187814328 | 63670 | Boot; directory and text reads |
| 2.9BSD, RL0 | 22108 | 1305665221 | 421632 | Multiuser root; RL/RP file writes, readback and sync |
| RSX-11M-PLUS, RQ1 | 3346 | 1637179270 | 786149 | STARTUP; DU devices; directory listing |

The RSX console log includes `PIP DU1:[1,54]RSX11M.SYS/LI`,
`RSX11M.SYS;1 1026. C`, and a total of 1026 blocks in one file.
The separate RQ0 run validates the original nonbootable-volume message and
HALT at PC `000034`; it is recorded as `expected_nonbootable`, not an OS boot.
That JED uses 4173 LUT, 1205 FF and 26 EBR; timing passes at 24 MHz.
At that qualification stage it had not yet been programmed onto the physical board.
