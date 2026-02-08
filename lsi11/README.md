## Devices (LSI-11 / PDP-11/03-like)

All addresses, vectors, and priorities are **octal**.

### Memory map (fixed, no MMU)
| Range (octal) | Size | Meaning |
|---|---:|---|
| `000000–157777` | 56 KB | RAM |
| `160000–177777` | 8 KB | I/O page (device CSRs only) |

### Device CSR / IRQ table
| Device | CSR address range(s) (octal) | Vector (octal) | Priority (octal) | DONE bit | IE bit | Software clears DONE | Notes |
|---|---|---:|---:|---|---|---|---|
| DL11 (console RX) | `0177560–0177563` and alias `0176500–0176503` | `000060` | `4` | RCSR bit 7 | RCSR bit 6 | Read RBUF (`...62`) | RX IRQ does not repeat until RX DONE cleared |
| DL11 (console TX) | `0177564–0177567` and alias `0176504–0176507` | `000064` | `4` | TCSR bit 7 | TCSR bit 6 | Write TBUF (`...66`) | TX IRQ does not repeat until TX DONE cleared |
| KW11-L (line clock) | `0177546–0177547` | `000100` | `6` | CSR bit 7 | CSR bit 6 | Any write to CSR low byte (`0177546`) | 50 Hz; **tick only if DONE=0** |
| RK11 (RK05) | `0177400–0177417` | `000220` | `5` | RKCS DONE/RDY (impl-defined) | RKCS IE | Write RKCS with GO=1 | Minimal READ+WRITE+GO |
| LP11 (printer) | `0177514–0177517` | `000200` | `4` | CSR bit 7 | CSR bit 6 | Write DBR (`0177516`) | Output to host stdout |
| SR (switch reg) | `0177570–0177571` | — | — | — | — | — | Read-only 16-bit value, default `000000` |

### Interrupt behavior (implemented)
- DL11 RX/TX, RK11, LP11 use DONE/IE with latched request semantics.
  CPU ACK clears only request latch; request is re-asserted on a new device event
  (or when IE is enabled while DONE is already set for DL11).
- KW11-L is periodic (50 Hz): ACK clears current request and DONE, then the next tick
  creates a fresh DONE/IRQ cycle.

## Known Limitations
* **No MMU**: 16-bit addressing only.
* **RK11**: Minimal implementation supporting READ/WRITE/GO for bootstrapping RT-11. (Format/Check commands not implemented).
* **Timing**: Not cycle-accurate. DL11 TX completion is instruction-paced (no host wall-clock wait by default).
* **LP11**: Output redirects to host stdout.

## Build and Test
* `make` builds the emulator binary `lsi11`.
* `make test` builds and runs the semantic tests (checks bus mapping and IRQ rules).
* `sh run_demo_tests.sh` runs demo peripheral tests (non-interactive) and uses
  `-exit-on-abort` so each demo stops at HALT.

## CPU Model Selection
By default `lsi11` starts with `dcj11` core mode.

You can select CPU model explicitly:
```
./lsi11 -cpu dcj11
./lsi11 -cpu 11/03
./lsi11 -cpu k1801vm1
./lsi11 -cpu k1801vm1g
./lsi11 -cpu k1801vm2
./lsi11 -cpu k1806vm2
```

Short aliases are also accepted:
- `vm1` for `k1801vm1`
- `vm1g` for `k1801vm1g`
- `vm2` for `k1801vm2`

## Booting RT-11 (RT11SJ)
The RT-11 image `disks/rt11v400.dsk` can be booted via the built-in loader:
```
./lsi11 -rk disks/rt11v400.dsk -bootrt11
```
This copies the first 512 bytes (or second block if the first is empty) into RAM at `000000`
and jumps to `000000`. If the image uses 1-based sector numbering, the second block is tried automatically.

Switch register can be set explicitly:
```
./lsi11 -rk disks/rt11v400.dsk -bootrt11 -sr 0
```
(`SR` is `0177570`, default `000000`).

Useful debug options:
- `-trace` instruction trace
- `-trace-regs` trace with registers
- `-traceirq` IRQ delivery trace
- `-tracenxm` bus/NXM trap trace

Execution control:
- `-exit-on-abort` terminate emulator loop when core sets `HALT/abort`
  (by default emulator continues and clears abort latch each loop).
