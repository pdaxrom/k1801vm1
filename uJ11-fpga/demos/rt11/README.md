# HC1200 panel and HG programs for uJ11

These are byte-for-byte copies of the existing lsi11-fpga applications, with
source hashes in `inputs.json`. They use the same CSR 166000/166001 and require
no display framebuffer, keyboard scanner or HG packet engine in the FPGA.

- `RUN DSPDEM`: two HCMS-3917 displays (16 characters).
- `RUN RGBDEM`: display messages and all eight RGB states.
- `RUN KEYDEM`: all twenty matrix keys and their raw key codes.
- The exit key is decoded as octal 023 (0x13). The demo text calls it ESC;
  the user's actual panel has MEM/PREV/NEXT/ENTER, with no separate ESC.
  Its physical exit-key label must be checked against the scanner code.
  [User-provided layout and planned ODT controls](../../docs/panel-keyboard.md).

Put JTAG_EN in GPIO mode after FPGA programming. HG and keyboard scanning share
the four JTAG pads and cannot run concurrently. HG releases the keyboard columns
before driving TDO; the FPGA reset releases TDO and blanks the displays.

The copied `hostdisk/HG.SYS` supports unmapped RT-11SJ/FB V5.03 (H.GEN=5).
Use the existing `../../lsi11-fpga/host/hg` host daemon; it is shared software,
not another FPGA resource. From the RT-11 monitor use `INSTALL HG`, `LOAD HG`
and `DIR HG:`. HG is a data disk, not the SD boot device. Start the daemon only
after JTAG_EN is in GPIO mode and the panel demo has exited. Stop it before
returning to keyboard scanning or JTAG programming.

Detailed source-project documentation:
- [Panel protocol](../../../lsi11-fpga/docs/PANEL-IO.md)
- [HG daemon and handler](../../../lsi11-fpga/demos/rt11/hostdisk/README.md)

The original SD image is preserved. A local test copy with the four binaries is
`build/rt11-uj11-panel-hg.dsk`; deployment of FPGA FLASH does not replace SD media.
The physical SD card inspected during CP29 already contains all four programs.

CP29 hardware: RGB/HDSP, keyboard input and demo exit are user-confirmed; HG DIR, COPY and
TYPE readback passed at 1000 Hz. The test host daemon was stopped after testing.
