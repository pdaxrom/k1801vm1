# HC7000 storage boot validation

The requested configurations use the original files in `lsi11/disks`:

| Case | Attachments | Requested boot |
|---|---|---|
| RT-11 XM V5.03 | RH0 `rt11v5.3/system.dsk`, RH1 `rt11v5.3/storage.dsk`, RK0 `basic.dsk`, RK1 `pascal.dsk`, RK2 `fortran.dsk` (last three also under `rt11v5.3`) | RH0 |
| RSX | RQ0 `rsx11/rsxm11sys.dsk`, RQ1 `rsx11/rsx11mpbl87.dsk` | RQ0 |
| RT-11 V4 | RK0 `rt11v400.dsk` | RK0 |

SIMH uses private copies; RTL uses fresh partitioned SD files with a read-only
backing file and a RAM write overlay. Source hashes are checked after each run.
No physical board or SD card is modified by these tests.

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

## Current FPGA result

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
under `releases/hc7000-bsd/validation/boot-matrix/`; current shared-I/O RTL
evidence is under `releases/hc7000-serv-io/validation/`.
