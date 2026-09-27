uJ11 HC7000 RT-11XM SD KIT - 27-SEP-2026

Target: LCMXO2-7000HC-4TG144C, hardware-lcd with 2 MiB SRAM.
12 MHz oscillator, 24 MHz CPU. UART 115200 8N1, no flow control.
Use the CPU=mmu firmware with the TRAP-mode fix (build 08 or newer).
The boot blocks select DM0:RT11XM. No manual BOOT switch is needed.

This is a raw RK07 disk: 53790 sectors of 512 bytes, no partition table.
The SD card is written with the uncompressed image starting at sector 0.
Keep a backup of a working card before replacing its contents.

Included utilities:
  B81FPU.SAV   BASIC with FP11 single precision (24-bit significand)
  B81FPD.SAV   BASIC with FP11 double precision (56-bit significand)
  B81TST.BAS   29 numerical checks, expected ERRORS=0 / CP81 PASS
  B81DBL.BAS   Double-precision checks, expected CP81 DOUBLE PASS
  HGX.SYS      Mapped HG host disk driver for XM
  HGTIME.SAV   Set system date/time from the HG host
  HGSYNC.COM   LOAD HG and RUN HGTIME
  TMRATE.SAV   Monitor clock snapshots, Q or ESC exits
  *.MAC        Sources for HGX, HGTIME, TMDRV and TMRATE
  XMCHK.COM   SHOW CONFIGURATION and SHOW MEMORY
  *.TXT       These on-disk manuals (ASCII with CRLF)

The original DEC tools, libraries, monitors and handlers are retained.
Use HELP, DIR, PIP, DUP, MACRO, LINK, EDIT and BINCOM as usual.
The original FB monitor remains available, but XM is the kit default.

FPP and ODT are part of the FPGA microcode. UJMOD, UJBOOT, ODT.BIN,
FP11.BIN and the old HALT-space tools are not used by this CPU profile.
Panel/HDSP applications are omitted: this target currently uses SD/UART.
For the HG host disk and time synchronization, see HOST.TXT.
For native rebuild commands, see BUILD.TXT.

Quick check:
  @XMCHK
  RUN B81FPU
  A
  RUN B81TST
  PRINT 1/0
  PRINT 2+2
  BYE
BASIC must report DIVISION BY ZERO, return READY, then print 4.

END README.TXT
