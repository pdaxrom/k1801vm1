# Physical installation: HC7000 CPU events, 2026-09-28

> Историческая запись: `releases/hc7000-cpu-events-hardware/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


The [qualified CPU-events JED](../hc7000-cpu-events/README.md) is now installed
on the LCMXO2-7000HC-4TG144C. Lattice Programmer verified the JTAG chain and
completed FLASH Erase,Program,Verify without errors. JED checksum: **C820**.
System clock remains **24 MHz**, from the external **12 MHz** oscillator.

Before reprogramming, `sync` completed in the running BSD shell. The existing
SD layout/image was retained. After programming, the menu selected RL0 and
`rl(0,0)rlunix` booted Berkeley UNIX 2.9.1, reporting 1979072 bytes of usable
memory. XP and RL attached; `ls` listed the root directory and `sync` returned.
The board is left at the **single-user `#` prompt**. Ctrl+D enters multiuser
mode. Physical multiuser/RQ tests were not run in this installation session.

The user's picocom process was resumed. UART settings were restored to
115200 8N1 with ISTRIP enabled for BSD's software parity. `uart.bin` is the
raw capture; `uart.txt` removes bit-7 parity and DEL padding for readability.
Programming logs, the exact deployment script and SHA256SUMS are included.
