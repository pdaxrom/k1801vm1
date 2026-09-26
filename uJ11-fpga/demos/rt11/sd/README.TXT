uJ11 SD SOFTWARE KIT - 2026-09-26

RT-11FB V05.03, HC1200, 128 KiB FRAM, no MMU.
UART: 115200 baud, 8 data bits, no parity, 1 stop bit, no flow control.
This is a raw RK07 image starting at SD sector 0, not a FAT file.
Write the decompressed image to the WHOLE SDHC/SDXC card on the host.
The project guide docs/sd-image.md explains writing and verification.

Type commands at the RT-11 dot prompt. Do not type the dot itself.
Use SY: explicitly if you have changed the default DK: device.
TYPE SY:filename.TXT reads a manual. Ctrl-S pauses; Ctrl-Q resumes.

DOCUMENTS AND PROGRAMS
  UJMOD.TXT   UJMOD.SAV: install, enable, disable and list FRAM modules
  UJBOOT.TXT  UJBOOT.SAV: software reboot through the resident bootstrap
  ODT.TXT     ODT.BIN: UART/panel debugger, steps and breakpoints
  FP11.TXT    FP11.BIN: optional software DCJ11 FPP
              SDBOOT.BIN: optional replaceable SD bootstrap (UJMOD.TXT)
  HG.TXT      HG.SYS and HGTIME.SAV: host disk and system time sync
  CLOCK.TXT   CLOCK.SAV / CLOCK.REL: HDSP clock, including FB foreground
  PANEL.TXT   DSPDEM.SAV, KEYDEM.SAV, RGBDEM.SAV: panel demonstrations
  BASIC.TXT   B81FIJ, B81FIS, B81FPU, B81FPD and two BASIC test programs
  TESTS.TXT   TMRATE.SAV, ODTCHK.SAV, FPTST.SAV: diagnostics

FIRST START
  The disk boots without optional FRAM modules. FIS is in microcode.
  Existing valid AUTO modules in FRAM remain active after normal reset.
  Copying BIN files to SD does not install/update them in HALT FRAM.
  RUN SY:UJMOD, then STATUS, shows the existing eight slots.
  Read UJMOD.TXT before changing an existing installation.
  The startup file does not install FRAM modules or start the clock.
  It disables SL and prints this kit's short welcome.

QUICK START
  RUN SY:B81FIJ  - FIS BASIC, usable both with and without FPP.
                  Answer A to the optional-functions question.
                  Type BYE at READY to return to RT-11.
  RUN SY:HGTIME  - sync RT-11 time after LOAD HG and starting the host.
  FRUN SY:CLOCK  - HDSP clock with a separate background console.
                  Ctrl-B selects the background console.

PANEL AND RECOVERY
  Select GPIO with JTAG_EN and unplug the programmer from shared pins.
  Stop the HG host daemon and UNLOAD HG before using panel keys.
  Short RESET enters installed/initialized ODT; C continues execution.
  Long RESET (about two seconds) restarts and checks retained modules.
  UART ESC immediately after long RESET bypasses all optional modules.
  The built-in fallback still needs a readable bootable SD card.

The standard DEC tools from the source distribution remain on the disk:
DIR, PIP, DUP, EDIT, MACRO, LINK, LIBR, HELP and others.
The boot monitor is FB. Included SJ/XM distribution files do not imply
XM support: the current uJ11 FPGA has no enabled MMU.
Text manuals use ASCII/CRLF for the standard RT-11 terminal.
