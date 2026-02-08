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

### RAM size model
- Default RAM size: **4096 KB**.
- RAM size is configurable with `-ram <kb>`.
- Host allocation/mapping page size is fixed:
  - `PAGE_SIZE = 4096` bytes (4 KB)
- `-ram <kb>` is validated on 8 KB segment granularity:
  - `ram_kb % 8 == 0`
- Invalid sizes are rejected with a clear error.
  - Example: `4100 KB` invalid
  - Example: `4104 KB` valid

### I/O compatibility
- RK11/DL11/KW11/LP11/SR are supported.
- DL11 alias is **not forced** by default on this target.
- If needed for compatibility, alias can be explicitly enabled:
  - `-dl11-alias`

### Examples
- Default config check:
  - `./pdp1134 -check-config`
- Explicit RAM size:
  - `./pdp1134 -ram 4104 -check-config`
- RT-11 boot:
  - `./pdp1134 -rk disks/rt11v400.dsk -bootrt11`

## Device CSR / IRQ table

| Device | CSR address range(s) (octal) | Vector (octal) | Priority (octal) | DONE bit | IE bit | Software clears DONE | Notes |
|---|---|---:|---:|---|---|---|---|
| DL11 (console RX) | `0177560–0177563` (+ optional alias `0176500–0176503`) | `000060` | `4` | RCSR bit 7 | RCSR bit 6 | Read RBUF (`...62`) | RX IRQ does not repeat until RX DONE cleared |
| DL11 (console TX) | `0177564–0177567` (+ optional alias `0176504–0176507`) | `000064` | `4` | TCSR bit 7 | TCSR bit 6 | Write TBUF (`...66`) | TX IRQ does not repeat until TX DONE cleared |
| KW11-L (line clock) | `0177546–0177547` | `000100` | `6` | CSR bit 7 | CSR bit 6 | Any write to CSR low byte (`0177546`) | 50 Hz; tick only if DONE=0 |
| RK11 (RK05) | `0177400–0177417` | `000220` | `5` | RKCS DONE/RDY (impl-defined) | RKCS IE | Write RKCS with GO=1 | Minimal READ+WRITE+GO |
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
