# CP29: uJ11 on the physical HC1200 board

2026-09-10. The physical LCMXO2-1200HC board on `sash@192.168.1.108`
was programmed with uJ11. **FLASH Erase/Program/Verify succeeded. RT-11FB
V05.03 booted from its existing SD card through SPI FRAM; DIR completed
and returned the prompt.** The user confirmed that RGB LED and HDSP/HCMS
messages work while `RGBDEM` runs. After switching JTAG_EN to GPIO, the user
also confirmed key codes change in `KEYDEM` and panel ESC returns to RT-11.
**HG read and write also passed on real hardware at 1000 Hz** after the
keyboard program exited. `DIR HG:` listed the test volume; RT-11 copied
`HELLO.TXT` to `ROUND.TXT` on the host disk. Both 512-byte exported files
match byte-for-byte (SHA256 `4d7985a77f29d7d0da2822f3645b5d432bd4aff022c098c35fcb02838e9bc997`).
`TYPE HG:ROUND.TXT` read back the expected text and returned the prompt.
The task then stopped its HG daemon to release the keyboard/JTAG pads.

## RTL and programs

The CP28 raw panel register was already connected to the actual SG32 pads.
CP29 extracts it into `boards/hc1200/uj11_panel.v` and adds two flip-flops
per asynchronous keyboard/HG input. Software reads only the second stage.
CSR 166000/166001, output latch, byte lanes and reset behavior are unchanged.
Reset blanks the displays, deselects HCMS and disables the TDO output driver.
No framebuffer, hardware font, keyboard scanner or HG packet engine was added.

`demos/rt11` now contains the existing readable MACRO-11 sources and built
DSPDEM/RGBDEM/KEYDEM/HG binaries. Their source hashes are recorded. The real
SD card already had these programs, so its image was not replaced. Physical
DIR listed **117 files, 2292 blocks and 51364 free blocks**. The pre-existing
`-BAD-` directory date fields are not an FPGA transfer error.

The shared HG daemon was built from the current lsi11-fpga host sources on
Linux, with its protocol/image/directory tests passing. Missing libftdi headers
were extracted from the Ubuntu package into the task's temporary directory;
system packages were not changed. The daemon was started only after GPIO selection and exit from the keyboard
program. [Host transfer log](../tb/reports/cp29/hg-live-2.log),
[readback UART](../tb/reports/cp29/hg-readback-uart.txt) and
[round-trip result](../tb/reports/cp29/hg-roundtrip.json) are archived.
The existing source emits a GCC format-truncation warning in rt11fs.c; host
protocol/image/directory tests passed. No host source was modified for this test.

## Measured synthesis and exact flashed artifact

| Revision | LUT4 | FF | EBR | Slices | TRACE Fmax | 29.56 MHz |
|---|---:|---:|---:|---:|---:|---|
| CP28m | 1217 | 318 | 6 | 610 | 31.186 MHz | PASS |
| CP29a | 1239 | 326 | 6 | 621 | 30.609 MHz | PASS |

Clean MAP/PAR/TRACE completed, all connections routed, zero setup/hold timing
errors. The synthesis delta is +22 LUT4, +8 FF and +11 slices for this revision;
there are only **41 LUT4, 19 slices and 1 EBR** left. The desired <=1100 LUT
budget and 50 MHz target remain unmet. External pin-delay and oscillator
worst-case tolerance closure are not claimed by the nominal-frequency result.

The source-matched [JED](../synth/reports/cp29a/design.jed) was exported only
after checking current source and raw report SHA256 values against the
completed synthesis manifest. Programmer first verified the target JTAG ID.
A fresh XCF was generated from that JED's actual timestamp and checksum.

- JED SHA256: `fa131b809f5f75a92b29c69acf18d57d8862e8467b31073bfaafa17d7ac988d4`
- JEDEC checksum: `E682`
- Input revision SHA256: `73dafeaa80cd42b9f4282012c29e19fa2e011e28485e596bd5c4ece2862aced5`
- [Programmer log](../tb/reports/cp29/hardware-1-programmer.log)
- [Physical boot + DIR UART](../tb/reports/cp29/hardware-1-uart.txt)
- [Physical RGBDEM UART](../tb/reports/cp29/panel-live-uart.txt)

The original picocom process exited before programming. UART was confirmed
unowned, then the task's reader captured boot and commands and released it.
No user console process was terminated.

The first HG command batch captured no UART response; Ctrl-C restored the
monitor, and separate LOAD/DIR completed. A later COPY exceeded the initial
55-second capture at 1 kHz but completed in the daemon and produced the matching
file. These early captures are retained, not marked as successful prompt checks.
The capture helper now requires the specific command echo and a subsequent
monitor prompt, and leaves an unowned UART in raw 115200 between calls instead
of restoring its old 9600/echo configuration. Saved termios is restored only when
resuming an existing reader. Final DIR, TYPE and SHOW DEVICE all passed the
prompt gate. The precise cause of the first empty capture is not proven.

To run the same host service again on the current Linux test host:

```sh
/tmp/uj11-cp29-a/host-hg/hgfsd --directory /tmp/uj11-cp29-a/hg-share --clock 1000
```

The test daemon is stopped at the end of CP29; RT-11 is left at its prompt
with HG loaded. Stop the daemon before keyboard scanning or JTAG programming.

## Verification scope

`make test-board-panel` drives the actual physical top's bus and observes
shared serial pins and the TDO pad: **2958 bus beats**, eight 0x4c HCMS command
bytes, a complete 640-bit dot frame, all eight RGB patterns, twenty row/column
positions, HG input mapping and TDO high/low/Z/reset behavior. This is a pin
contract test, not a claim of full HG packet/RT-11 handler simulation.

The existing 29-beat board bus regression and CP28 portable/vendor ROM, IRQ,
FRAM and exact KW11 timebase tests passed with the new RTL. Cold Verilator
RT-11 boot + DIR again passed with **355132188 CPU clocks, 3983731 retirements,
3270 UART bytes and 162 SD reads / 6 writes**. The original image is unchanged.

The added image `build/rt11-uj11-panel-hg.dsk` is a local copy, reproducible
with `python3 tools/build_panel_image.py --output NEW_IMAGE.dsk`, containing the
four applications; all four extracted binaries match the sources byte-for-byte,
and cold boot/DIR passed in simulation (102 files, 366214535 CPU clocks,
163 SD reads and 6 writes). A second generated copy matched byte-for-byte. It was not written to the physical SD.
The repository's `rt11tool fsck` reports the same two directory/EOS issues on
both the original and this copy; no repair was applied to either. RT-11 itself
reads both directories. This check is not reported as a clean filesystem fsck.

The unmodified Lattice DP8KC model cannot run in Verilator because it uses
procedural assign/deassign with blocking/nonblocking assignments. `run_board.py
--vendor` therefore uses Icarus; the full long vendor boot was not rerun.
The exhaustive vendor ROM tests remain part of the passed unit gate.

No CPU ISA or microcode change: 954/1024 words, 36 bits. FIS is present; FP11,
MMU, register banking and full-board prefetch remain absent.

## Reproduction

```sh
make board test-board-panel
python3 tools/check_board_units.py
python3 tools/run_board.py --tag cp29
# On Linux Diamond; use a fresh checkpoint for a new implementation:
make synthesis-board BOARD_CHECKPOINT=cp29b
python3 tools/export_board.py cp29b
```

For programming, regenerate XCF from the exact exported JED using
`tools/make_programmer_xcf.py`, then use the documented Lattice Programmer
FLASH Erase,Program,Verify operation. `tools/hardware_uart.py --xcf ... --out ...`
can capture boot and paced UART commands. JTAG_EN must be in JTAG mode for
programming and GPIO mode for keyboard/HG use. HG and keyboard scanning share
pins; finish the panel program before starting the host daemon.
