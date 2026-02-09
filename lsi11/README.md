## LSI-11 Subproject: Split Machine Profiles

All addresses, vectors, and priorities are **octal**.

This subproject now provides two separate executables:

1. `lsi11` (PDP-11/04-like profile)
2. `pdp1134` (PDP-11/34-like profile)

## 1) `lsi11` target (PDP-11/04-like, NO MMU)

### Identity and MMU policy
- Built with `ENABLE_MMU=0` (MMU not compiled in).
- This target is intentionally fixed-function and does not require MMU runtime setup.
- Built with `MMU_STUB_REGS_WHEN_DISABLED=0` so MMU CSR stubs do not overlap LP11 CSR addresses.
- RH11 is not registered in this profile (addresses `0177440..0177462` remain absent/NXM).

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

## 2) `pdp1134` target (PDP-11/34-like, MMU enabled)

### Identity and MMU policy
- Built with `ENABLE_MMU=1`.
- MMU registers and translation are available in CPU core.
- Default CPU model is `dcj11`.
- Optional override: `-cpu dcj11|11/03|11/34|k1801vm1|k1801vm1g|k1801vm2|k1806vm2`.
- DCJ11 CPU register `0177750` is implemented in `core/` (CPU-level), not in machine device map.

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
- RK11, RH11, DL11, KW11, LP11, SR are supported.
- DL11 alias is **not forced** by default on this target.
- If needed for compatibility, alias can be explicitly enabled:
  - `-dl11-alias`
- Bus decode model:
  - low 16-bit I/O page `160000..177777` is reserved for devices/NXM (not RAM)
  - higher 18-bit/22-bit I/O windows are decoded to the same 16-bit device page
  - RAM is available below `160000` and above the low I/O page up to configured size

### RH11 in pdp1134 profile
- RH11 (Massbus adapter) is available only in `pdp1134`.
- CSR map (octal), 16-bit I/O page view, base `0177440`:
  - `0177440 RHCS1`, `0177442 RHWC`, `0177444 RHBA`, `0177446 RHDA`
  - `0177450 RHCS2`, `0177452 RHDS`, `0177454 RHER`, `0177456 RHAS`
  - `0177460 RHLA`, `0177462 RHDB`
- IRQ: vector `000254`, priority `5`.
- IRQ semantics follow project latch rules:
  - edge-triggered and latched, no repeat while `DONE=1`
  - ACK clears only `irq_req`
  - new `GO` clears `DONE` (software clear) and re-arms IRQ edge
  - IE toggles do not generate an IRQ by themselves
- Supported commands (phase 1):
  - `READ`, `WRITE`, `SEEK` (single unit, unit 0)
- DMA model (phase 1 limitations):
  - `RHBA` uses low 16-bit physical bus address only
  - no Unibus map / no 18/22-bit RH DMA extension
  - DMA uses bus physical accesses only, MMU translation is not used
  - NXM during DMA sets error and still completes command with `DONE`
- Disk mapping (phase 1):
  - flat image file
  - `RHDA[5:0]` sector, `RHDA[15:6]` linear track
  - `LBA = track * 64 + sector`, sector size `512` bytes (`01000` octal)

### Examples
- Default config check:
  - `./pdp1134 -check-config`
- Explicit RAM size:
  - `./pdp1134 --mem-kb 4104 -check-config`
- RT-11 boot:
  - `./pdp1134 -rk disks/rt11v400.dsk -bootrt11`
- RH11 image attach:
  - `./pdp1134 -rh disks/rk07.img`

## Device CSR / IRQ table

| Device | CSR address range(s) (octal) | Vector (octal) | Priority (octal) | DONE bit | IE bit | Software clears DONE | Notes |
|---|---|---:|---:|---|---|---|---|
| DL11 (console RX) | `0177560–0177563` (+ optional alias `0176500–0176503`) | `000060` | `4` | RCSR bit 7 | RCSR bit 6 | Read RBUF (`...62`) | RX IRQ does not repeat until RX DONE cleared |
| DL11 (console TX) | `0177564–0177567` (+ optional alias `0176504–0176507`) | `000064` | `4` | TCSR bit 7 | TCSR bit 6 | Write TBUF (`...66`) | TX IRQ does not repeat until TX DONE cleared |
| KW11-L (line clock) | `0177546–0177547` | `000100` | `6` | CSR bit 7 | CSR bit 6 | Any write to CSR low byte (`0177546`) | 50 Hz; tick only if DONE=0 |
| RK11 (RK05) | `0177400–0177417` | `000220` | `5` | RKCS DONE/RDY (impl-defined) | RKCS IE | Write RKCS with GO=1 | Minimal READ+WRITE+GO |
| RH11 (Massbus) | `0177440–0177462` | `000254` | `5` | RHCS1 bit 7 | RHCS1 bit 6 | Write RHCS1 with GO | READ/WRITE/SEEK, 16-bit RHBA DMA |
| LP11 (printer) | `0177514–0177517` | `000200` | `4` | CSR bit 7 | CSR bit 6 | Write DBR (`0177516`) | Output to host stdout |
| SR (switch reg) | `0177570–0177571` | — | — | — | — | — | Read-only 16-bit value, default `000000` |

## Build
- Build both profiles:
  - `make -C lsi11`
- Build only 11/04-like profile:
  - `make -C lsi11 lsi11`
- Build only 11/34-like profile:
  - `make -C lsi11 pdp1134`

## Tests
- 11/04-like tests (MMU disabled + DL11 alias checks):
  - `make -C lsi11 test-lsi11`
- 11/34-like tests (RAM sizing + core MMU suite):
  - `make -C lsi11 test-pdp1134`
- Full matrix:
  - `make -C lsi11 test-matrix`

## Common debug options
- `-trace` instruction trace
- `-trace-regs` trace with registers
- `-traceirq` IRQ delivery trace
- `-tracenxm` bus/NXM trap trace
- `-exit-on-abort` terminate emulator loop when core sets HALT/abort
