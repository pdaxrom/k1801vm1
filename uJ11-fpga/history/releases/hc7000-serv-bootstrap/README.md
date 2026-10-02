# HC7000: SERV installs the J11 bootstrap

> Историческая запись: `releases/hc7000-serv-bootstrap/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


2026-10-01 source and synthesis checkpoint, **not programmed on the board**.
The board still runs PAL 2AAE. This checkpoint keeps 200 PAL rows; the planned
240-row terminal is a separate change. No SD image was modified on hardware.

SERV RAM grows from 12 to **13 KiB**, using the EBR previously occupied by
the standalone J11 boot ROM. The same 430-byte `SDIOP.MAC` bootstrap is embedded
in SERV firmware and copied to physical SRAM at octal `004000` before J11 is
released from reset. Board reset reinstalls it; guest RESET preserves guest RAM.
See [architecture and memory layout](../../../docs/serv-bootstrap.md).

| Resource | Installed 2AAE | This checkpoint |
|---|---:|---:|
| LUT4 | 4911 | **4942 / 6864** |
| FF | 1791 | 1838 / 7209 |
| EBR | 26 | **26 / 26** |
| PLL | 2 | 2 / 2 |
| CPU clock | 50 MHz | 50 MHz |
| PAL clock | 64 MHz | 64 MHz |

[MAP/PAR/TRACE result](synthesis/result.json): fully routed, no negative slack;
CPU Fmax 50.994 MHz, PAL Fmax 64.990 MHz. SRAM pin constraints and PAL mailbox
constraints are scored and pass. MAP lists thirteen EBRs in `system/bus/disk/ram`
and no EBR in `system/bus/boot`.

The first 12 KiB keep full-width access; the last KiB uses two 16-bit beats
per 32-bit access, with all byte enables preserved. The final bank contains
the stack in this build. Code/constant bytes: 11191; BSS: 976; stack: 640;
gap between BSS and stack: 504. Firmware SHA-256:
`3ae5b5b50487db20d19b656300d8f0a890086fee648b0f20306e18e8c9a25dc1`.
The video-off firmware also builds in 13 KiB, with 11183 code/constant bytes.

| Validation | Result |
|---|---|
| [RAM and SERV execution](validation/ram/result.json) | 23300 RAM checks each in behavioral and official Lattice models; real RV32IC split instruction fetches across `0x2ffe` and in the last KiB at 24/50 MHz |
| [Full-board SD menu](validation/test-serv-bootstrap-menu50/result.json) | 14 cases at 50 MHz: all five controllers, timeout/Enter/cancellation, no default, direct boot, corrupt headers/payload/wire CRC, invalid size, board reset and guest RESET |
| [RH and invalid labels](validation/test-serv-bootstrap-storage/result.json) | Four cases; only the bootstrap destination is written before CPU release |
| [RK05/MSCP with active PAL](validation/test-serv-bootstrap-rk-rq/result.json) | 1049529 checks; stack writes reach 320/640 bytes |
| [RL/XP](validation/test-serv-bootstrap-rl-xp/result.json) | 370624 checks; stack writes reach 336/640 bytes |
| [PAL PDP-11 demo](validation/test-pal-demo/result.json) | 393216 SRAM bytes checked against text, page and scroll references; exit to HALT passes |
| [RT-11 V4 full-board boot](validation/rt11v4/result.json) | Actual five-second menu autoboot, RK0 bootstrap, DIR and TYPE; 2816 checks, 63885 DMA words; disk image unchanged |
| [HC1200 baseline](validation/hc1200.log) | Released RTL/ROM/software equivalence passes |

`sources/` preserves synthesis inputs and validation sources. `synthesis/inputs.json`
records their hashes; `SHA256SUMS` covers this checkpoint. The simulator's copies
of disk images are excluded. No JED is published here; physical qualification
and measurements of real OS performance with the new SERV bank remain pending.

Reproduce the principal checks from `uJ11-fpga` with:

```sh
export UJ11_MMU_FPP=off UJ11_MMU_IOP=storage UJ11_MMU_CLOCK_MHZ=50
export UJ11_HC7000_DIAGNOSTICS=1 UJ11_HC7000_VIDEO=1
python3 tools/test_storage_ram.py --vendor-library /path/to/diamond/cae_library/simulation/verilog/machxo2
python3 tools/test_storage_menu.py
python3 tools/test_storage.py
python3 tools/test_storage_rk_rq.py --fast-memory
python3 tools/test_storage_rl_xp.py
python3 tools/test_pal_demo.py
```
