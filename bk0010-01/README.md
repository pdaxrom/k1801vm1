BK-0010-01 emulator (core-based)
================================

This is a minimal SDL2 front-end that uses the existing `core/` CPU to execute
BK-0010-01 software. The hardware map is intentionally small: RAM + ROM overlay
+ a simple console device and a fixed monochrome framebuffer.

Build
-----

- `make -C bk0010-01`

Run
---

- `./bk0010-01 [--rom-dir <path>] [--monitor <file>] [--basic <file>] [--start <octal>] [--trace] [--show-shift] [--show-cpu]`

Notes
-----

- ROM directory defaults to `ROM/` and expects:
  - `MONIT10.ROM` mapped to 0100000–117777
  - `BASIC10.ROM` mapped to 0120000–177577
- PC starts at `--start` if provided, otherwise at 0100000.
- The framebuffer is a fixed 1bpp bitmap at `BK_VRAM_BASE` (octal 0100000).
  Size is `BK_VRAM_SIZE` (octal 040000 = 16KB), which maps to 512x256.
- The shift register (0177664) controls vertical scroll: low 8 bits are
  multiplied by 0100 (octal) bytes per TV line. Bit 9 enables RP mode and
  switches VRAM to 070000–077777 (4KB), with wraparound.
- Console I/O is a PDP-11 compatible DL11-style device:
  - RCSR: 0177560
  - RBUF: 0177562
  - TCSR: 0177564
  - TBUF: 0177566
  Output to TBUF is printed to stdout. Key presses feed RBUF.

TODO
----

- Replace the memory map with a full BK-0010-01 map (ROM/RAM banks, I/O).
- Implement proper keyboard matrix and video controller registers.
- Add timing/interrupts and tape/serial devices.
