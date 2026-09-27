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
| RT-11 XM, boot RH0 | Boot passes at 24 MHz with 2 MiB and 22-bit addressing; RH1 directory reads pass. RK0 returns `?DIR-F-Invalid device RK0:`. RK0..RK2 are not implemented. |
| RSX, boot RQ0 | SERV rejects the unsupported boot controller with startup status `c007` and enters ODT before any guest SD write. RQ/MSCP is not implemented. The RQ0 image also has the independent nonbootable-block issue described above. |
| RT-11 V4, boot RK0 | SERV reports `c007` and enters ODT before any guest SD write. RK05 is not implemented. |

The XM RTL run has 5503 checks, 364257064 clocks and 135715 DMA words. It
executes the actual CPU, SERV firmware, SPI SD model, SRAM pins and UART
waveforms, including the five-second menu default. The two unsupported-boot
tests verify the current failure behavior; they are **not successful OS boots**.
The complete requested matrix therefore remains incomplete on FPGA until
RK05 and RQ/MSCP support is added. No hardware or firmware changes were made
to disguise these outcomes.

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
UJ11_MMU_FPP=off UJ11_MMU_IOP=storage python3 tools/test_os_boot_rtl.py --out build/matrix-rtl
```

The RTL runner uses the packaged SD menu and the current hardware/firmware.
It distinguishes an OS boot from a successful test of an expected rejection;
`all_configurations_passed` remains false while RK05 and RQ/MSCP are absent.
Saved evidence is under `releases/hc7000-bsd/validation/boot-matrix/`.
