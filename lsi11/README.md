## LSI-11 Subproject: Split Machine Profiles

All addresses, vectors, and priorities are **octal**.

This subproject now provides two separate executables:

1. `lsi11` (PDP-11/04-like profile)
2. `pdp1184` (PDP-11/84-like profile, DCJ-11)

## 1) `lsi11` target (PDP-11/04-like, NO MMU)

### Identity and MMU policy
- Built with `ENABLE_MMU=0` (MMU not compiled in).
- This target is intentionally fixed-function and does not require MMU runtime setup.
- Built with `MMU_STUB_REGS_WHEN_DISABLED=0` so MMU CSR stubs do not overlap LP11 CSR addresses.
- RH11 is available in this profile at `0177440..0177462`.

### Memory map (fixed)
| Range (octal) | Size | Meaning |
|---|---:|---|
| `000000–157777` | 56 KB | RAM |
| `160000–177777` | 8 KB | I/O page (device CSRs only) |

### DL11 alias requirement
- DL11 alias mapping is enabled and preserved:
  - `0176500–0176507` aliases `0177560–0177567`

### Boot/usage
- RT-11 boot with RK11 loader:
  - `./lsi11 -rk disks/rt11v400.dsk -bootrt11`
- RH11 image attach:
  - `./lsi11 -rh disks/rk07.img`
- RL image attach (auto RL01/RL02 detect):
  - `./lsi11 -rl disks/rl02.dsk`

## 2) `pdp1184` target (PDP-11/84-like, MMU enabled)

### Identity and MMU policy
- Built with `ENABLE_MMU=1`.
- MMU registers and translation are available in CPU core.
- Default CPU model is `dcj11`.
- Optional override: `-cpu dcj11|11/03|11/84|11/34|k1801vm1|k1801vm1g|k1801vm2|k1806vm2`.
- CPU-visible processor registers are implemented in `core/` (CPU-level), not in machine device map:
  - `0177750` (DCJ11 register)
  - `0177776` (PSW)
- In `pdp1184` profile, `0177750` reset value is initialized to `001045` to match
  RT-11/SIMH PDP-11/84 CPU identification path.

### RAM size model
- Default RAM size: **4096 KB**.
- RAM size is configurable with `-ram <kb>` or `--mem-kb <kb>`.
- Host allocation/mapping page size is fixed:
  - `PAGE_SIZE = 4096` bytes (4 KB)
- RAM must be a multiple of page size:
  - `ram_kb % 4 == 0`
- Invalid sizes are rejected with a clear error.
  - Example: `4101 KB` invalid
  - Example: `4104 KB` valid

### I/O compatibility
- RL11, RK11, RH11, DL11, KW11, LP11, SR are supported.
- DL11 alias is **not forced** by default on this target.
- DL11 terminal character width defaults to **7-bit** (UNIX V5 compatible).
  - Use `-tty8b` for 8-bit console behavior.
- If needed for compatibility, alias can be explicitly enabled:
  - `-dl11-alias`
- Bus decode model:
  - low 16-bit page `160000..177777`: registered device CSRs are decoded there
  - undecoded addresses in `160000..177777` are NXM (reserved I/O page, not RAM)
  - higher 18-bit/22-bit I/O windows are decoded to the same 16-bit device page for registered devices
  - for non-device addresses outside RAM range, bus returns NXM as usual

### RH11 controller
- RH11 (Massbus adapter) is available in both `lsi11` and `pdp1184`.
- CSR map (octal), 16-bit I/O page view, base `0177440`:
  - `0177440 RHCS1`, `0177442 RHWC`, `0177444 RHBA`, `0177446 RHDA`
  - `0177450 RHCS2`, `0177452 RHDS`, `0177454 RHER`, `0177456 RHAS`
  - `0177460 RHLA`, `0177462 RHDB`
- IRQ: vector `000210`, priority `5` (RK611 default from EK-RK067-UG-001).
- IRQ semantics follow project latch rules:
  - edge-triggered and latched, no repeat while `DONE=1`
  - ACK clears only `irq_req`
  - new `GO` clears `DONE` (software clear) and re-arms IRQ edge
  - IE toggles do not generate an IRQ by themselves
- Supported commands (phase 1):
  - `READ`, `WRITE`, `SEEK` (single unit, unit 0)
- DMA model:
  - `RHBA` uses low 16-bit physical bus address
  - `RHCS1` bits `BA16/BA17` extend DMA to 18-bit physical address space
  - no Unibus map (22-bit map is not implemented)
  - DMA uses bus physical accesses only, MMU translation is not used
  - NXM during DMA sets error and still completes command with `DONE`
- Disk mapping (phase 1):
  - flat image file
  - `RHDA[5:0]` sector, `RHDA[15:6]` linear track
  - `LBA = track * 64 + sector`, sector size `512` bytes (`01000` octal)

### Examples
- Default config check:
  - `./pdp1184 -check-config`
- Explicit RAM size:
  - `./pdp1184 --mem-kb 4104 -check-config`
- RT-11 boot:
  - `./pdp1184 -rk disks/rt11v400.dsk -bootrt11`
- RH11 image attach:
  - `./pdp1184 -rh disks/rk07.img`
- RL image attach:
  - `./pdp1184 -rl disks/rl02.dsk`
- UNIX V5 boot:
  - `./pdp1184 -rk disks/unix_v5_rk.dsk -bootrt11`

## Device CSR / IRQ table

| Device | CSR address range(s) (octal) | Vector (octal) | Priority (octal) | DONE bit | IE bit | Software clears DONE | Notes |
|---|---|---:|---:|---|---|---|---|
| DL11 (console RX) | `0177560–0177563` (+ optional alias `0176500–0176503`) | `000060` | `4` | RCSR bit 7 | RCSR bit 6 | Read RBUF (`...62`) | RX IRQ does not repeat until RX DONE cleared |
| DL11 (console TX) | `0177564–0177567` (+ optional alias `0176504–0176507`) | `000064` | `4` | TCSR bit 7 | TCSR bit 6 | Write TBUF (`...66`) | TX IRQ does not repeat until TX DONE cleared |
| KW11-L / KW11-P | `0177546–0177547` (L), `0172540–0172545` (P) | `000100` | `6` | L: CSR bit 7 (monitor), P: CSR bit 7 (DONE) | L: CSR bit 6, P: CSR bit 6 | L: read CSR low byte (`0177546`), P: write CSR with DONE=0 | L: fixed 50 Hz line clock; P: programmable `single/repeat`, `up/down`, rates `100 kHz/10 kHz/60 Hz/external`, ERR in CSR bit 8 |
| RL11 (RL01/RL02) | `0174400–0174407` | `000160` | `5` | RLCS bit 7 (CRDY) | RLCS bit 6 | Start command by clearing CRDY (negative GO) | BAR/DA/MPR registers, BA16/BA17 in RLCS bits 4-5, commands: NO-OP/WCHK/GET STATUS/SEEK/READ HEADER/WRITE/READ |
| RK11 (RK05) | `0177400–0177417` | `000220` | `5` | RKCS RDY bit 7 | RKCS IDE bit 6 | Start next command (`GO=1`) | Control Reset/Read/Write/Write Check/Read Check/Seek/Drive Reset/Write Lock, RKER hard/soft errors, RKBA+MEX DMA |
| RH11 (RK611-compatible) | `0177440–0177462` | `000210` | `5` | RHCS1 bit 7 | RHCS1 bit 6 | Write RHCS1 with GO | READ/WRITE/SEEK + RK611 command subset, RHBA + BA16/BA17 DMA |
| LP11 (printer) | `0177514–0177517` | `000200` | `4` | CSR bit 7 | CSR bit 6 | Write DBR (`0177516`) | Output to host stdout |
| SR (switch reg) | `0177570–0177571` | — | — | — | — | — | Read-only 16-bit value, default `000000` |

## Build
- Build both profiles:
  - `make -C lsi11`
- Build only 11/04-like profile:
  - `make -C lsi11 lsi11`
- Build only 11/84-like profile:
  - `make -C lsi11 pdp1184`
- Backward-compatible alias:
  - `make -C lsi11 pdp1134`

## Tests
- 11/04-like tests (MMU disabled + DL11 alias checks):
  - `make -C lsi11 test-lsi11`
- 11/84-like tests (RAM sizing + core MMU suite):
  - `make -C lsi11 test-pdp1184`
- Backward-compatible alias:
  - `make -C lsi11 test-pdp1134`
- Full matrix:
  - `make -C lsi11 test-matrix`

Compatibility note:
- Binary `pdp1134` is kept as an alias of `pdp1184` for existing scripts.

## Common debug options
- `-trace` instruction trace
- `-trace-regs` trace with registers
- `-traceirq` IRQ delivery trace
- `-tracenxm` bus/NXM trap trace
- `-exit-on-abort` terminate emulator loop when core sets HALT/abort

## Device disable options
- `-disable-dl` disable DL11
- `-disable-kw` disable KW11
- `-disable-lp` disable LP11
- `-disable-rk` disable RK11
- `-disable-rh` disable RH11
- `-disable-rl` disable RL11
- `-disable-sr` disable SR register

Notes:
- If a device is disabled, attaching media for it (for example `-disable-rl` with `-rl`) is rejected.
- `-check-config` prints final per-device enable state (`dev_*` fields).

## DCJ11 K/S/U Compliance (In Progress)

Checklist:
- [ ] PSW `CM/PM` handling for DCJ11 (`CM=<15:14>`, `PM=<13:12>`).
- [ ] Separate stack banks `KSP/SSP/USP` and active `R6` switch by `CM`.
- [ ] IRQ/trap entry frame correctness (`OLDPS`, `OLDPC`) on handler stack.
- [ ] `RTI/RTT` restore correctness (PC/PSW order + stack-bank handoff).
- [ ] Vector fetch path is physical (MMU bypass).
- [ ] `MFPI/MFPD/MTPI/MTPD` use previous mode (`PM`) with proper I/D space.

### Verification Results

- Pending in C5 (commands and expected outputs).
