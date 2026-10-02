# Physical RSX test on HC7000, JED 7123

> Историческая запись: `releases/hc7000-microcode-return/baseline-physical-7123/rsx/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../../../README.md).


On 2026-09-29 the board booted RSX-11M-PLUS V4.6 BL87 from RQ1 after a
long front-panel RESET. STARTUP completed. `DEV DU:` showed DU0 and public
mounted DU1, label RSX11MPBL87. `PIP DU1:[1,54]RSX11M.SYS/LI` returned
RSX11M.SYS;1, 1026 blocks. The successful capture is
[rsx-7123-20260929-menu/uart.txt](rsx-7123-20260929-menu/uart.txt), with
[session.json](rsx-7123-20260929-menu/session.json) reporting `passed: true`.
The runner's `cold_boot` flag means a fresh bootstrap rather than an OS resume;
the successful capture was triggered by a **long button RESET**, not a power cycle.

The two earlier failed captures are preserved: `rsx-7123-20260929` hit the
USB device/udev permission race before receiving bytes;
`rsx-7123-20260929-reset` rejected the older menu's RQ description before
selecting RQ1. Neither attempt reached an RSX execution failure.
`hardware-rsx.py` is the successful runner; `hardware_modules.py` is its
UART helper, identified by the session's SHA-256. The successful capture
accepts both menu descriptions and waits for serial-device access.

[sd-rq-20260929/result.json](sd-rq-20260929/result.json) records source-image
hashes, device identity, partition positions and before/after hashes for
RL0, RL1 and XP0. Both added RQ payloads were verified by readback, and all
BSD partitions were byte-for-byte preserved. RL0 remains the default boot.
This records the SD state immediately after installation; RSX may write its
own RQ1 volume during subsequent boots.

`add-rq-card.py` is the exact installation script. Compressed backups of the
original reserved prefix and overwritten RQ ranges remain on the Linux host
at `/home/sash/Work/FPGA/uj11-serv-compact-20260928/uJ11-fpga/build/cpu-next/sd-rq-20260929/`.
Their byte ranges and uncompressed hashes are in the installation record;
the media backups themselves are not committed here.

At the end of this baseline test the board ran **7123** and was left at
the RSX `>` prompt with the UART closed by the test. It was subsequently
[upgraded to 1B7F](../../hardware-1b7f/README.md). These RSX hardware results
qualify 7123; the 1B7F installation has a separate physical BSD test.
