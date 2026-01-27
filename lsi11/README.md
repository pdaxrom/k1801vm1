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
| RK11 (RK05 disk) | `0177400–0177417` | `000220` | `5` | RKCS DONE/RDY (impl-defined) | RKCS IE | Write RKCS with GO=1 | Minimal READ+GO for bootstrap |
| LP11 (printer) | `0177514–0177517` | `000200` | `4` | CSR bit 7 | CSR bit 6 | Write DBR (`0177516`) | Output to host stdout |
| SR (switch reg) | `0177570–0177571` | — | — | — | — | — | Read-only 16-bit value, default `000000` |

### Interrupt rule (global)
**An interrupt request must not repeat while the corresponding DONE/status bit remains set.**
CPU ACK clears only the internal irq request latch; DONE is cleared only by the “Software clears DONE”
operation listed above.

## Known Limitations
* **No MMU**: 16-bit addressing only.
* **RK11**: Minimal implementation supporting only READ/WRITE/GO for bootstrapping RT-11. (Format/Check commands not implemented).
* **Timing**: Peripheral timing is approximate (host wall clock), not cycle-accurate.
* **LP11**: Output redirects to host stdout.

## Build and Test
* `make` builds the emulator binary `lsi11`.
* `make test` builds and runs the semantic tests (checks bus mapping and IRQ rules).
