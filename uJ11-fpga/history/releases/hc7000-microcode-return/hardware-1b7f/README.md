# HC7000 physical installation of JED 1B7F

> Историческая запись: `releases/hc7000-microcode-return/hardware-1b7f/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../../README.md).


Installed on 2026-09-29. Lattice Programmer completed **FLASH
Erase,Program,Verify without errors**, in 55 seconds. Before programming,
all synthesis inputs/reports and the JED hash were verified. The device is
LCMXO2-7000HC-4TG144C, external oscillator 12 MHz, CPU clock **50 MHz**,
FPP off, SERV storage IOP. JED SHA-256:
`1cfce5454efa6001b2ff75086af04b47849f71bfba311663cb92be8e6b75863a`.

The running RSX was stopped using `RUN LB:[3,54]SHUTUP`, zero minutes,
reason `FPGA update 1B7F`, and confirmation Y. Its output confirms that
DU1 was dismounted and SHUTUP completed before programming began.

The board automatically booted RL0 after programming. The physical test
verified Berkeley UNIX 2.9.1, 1979072 bytes of available memory, multiuser
root login, `/usr` on XP0, `/etc/fstab`, and creation/readback/removal of
separate files on RL0 and XP0 followed by `sync`. Temporary files and
directories were removed. BSD was left at the root prompt.

- [Session result](hardware-1b7f-20260929/session.json): `passed: true`.
- [Readable UART](hardware-1b7f-20260929/uart.txt); raw bytes are preserved
  alongside it in `uart.bin`. The readable form removes the parity bit and DEL.
- [Programmer output](hardware-1b7f-20260929/programmer-stdout.log), XCF,
  JTAG enable log, and original Programmer log are in the same directory.
- `flash1b7f.py` is the exact executed script; UART and XCF helpers are included.

The serial input was returned to ISTRIP for BSD software parity and BSD
was configured with `stty nl0 cr0`. `console.json` records the host flag
change. The existing picocom process was resumed; use `fg` in its original
Linux shell to return it to the foreground.

Physical RSX boot/directory evidence in `../baseline-physical-7123/rsx/`
belongs to **7123**. This 1B7F installation validates BSD on hardware;
RSX on 1B7F has passed the packaged RTL tests.
