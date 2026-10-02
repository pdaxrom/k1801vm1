# Physical installation: HC7000 50 MHz, 2026-09-28

> Историческая запись: `releases/hc7000-50mhz-hardware/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


The [qualified 50 MHz JED](../hc7000-50mhz/README.md) is now installed on the
LCMXO2-7000HC-4TG144C. Lattice Programmer verified the JTAG chain and completed
FLASH Erase,Program,Verify without errors. JED checksum: **74B5**; SHA-256:
`ea7e6da4cd9cc5e8012ded5596d6f03964c097f58865db98e0fb8b35c8f14d53`.
The system clock is configured for **50 MHz** from the external **12 MHz**
oscillator. This is not an oscilloscope frequency measurement.

Before programming, `sync` completed in the running BSD shell. The existing
SD layout and images were retained. After programming, the SD menu selected
RL0 and `rl(0,0)rlunix` booted Berkeley UNIX 2.9.1, reporting 1979072 bytes of
usable memory. XP and RL attached; `ls` listed the root directory and `sync`
returned. The board is left at the **single-user `#` prompt**. Ctrl+D enters
multiuser mode. Physical multiuser/RQ tests were not run in this installation;
their RTL results are preserved in the qualified build snapshot.

SIGCONT was sent to the user's picocom process after capture. The shell had
regained foreground ownership of `pts/0`; picocom subsequently appeared as
a stopped background job. Run **`fg` in that shell** to return to picocom.
UART is 115200 8N1 with ISTRIP enabled for BSD's software parity.
`flash/uart.bin` contains the raw capture;
`flash/uart.txt` removes bit-7 parity and DEL padding for readability.
Programming logs, deployment code, host/terminal state and SHA256SUMS are included.

The earlier build snapshot records the pre-installation state; this directory
records its subsequent successful physical installation.
