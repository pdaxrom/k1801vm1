# CPU Core (`core/`)

All PDP-11 values in this document are octal.

## Scope
This directory documents and implements CPU-level behavior only:
- instruction execution and flags
- trap/exception and interrupt entry/return semantics
- model-specific CPU registers (DCJ-11, VM1, VM2/K1806VM2)
- MMU translation/protection logic when `ENABLE_MMU=1`

Out of scope for `core/`:
- machine bus wiring details
- peripheral controllers (DL11/RK11/RH11/RL11/TQ11/KW11/etc.)
- CLI options and boot media workflows

Those are documented in [`lsi11/README.md`](../lsi11/README.md).

## CPU Models
- `DCJ11` (`11/84` profile CPU)
- `K1801VM1`
- `K1801VM2`
- `K1806VM2` (alias/variant handling via VM2 paths)

## Addressing/MMU Policy
- Build with `ENABLE_MMU=0`:
  - CPU runs without MMU translation (identity-like behavior through machine bus callbacks).
- Build with `ENABLE_MMU=1`:
  - Kernel/Supervisor/User modes and PAR/PDR sets are active.
  - Split I/D handling follows implemented model rules.
  - 22-bit path is supported for DCJ-11 MMU-enabled operation.

## Recent DCJ11 Compatibility Fixes
- DCJ11 stack limit register `0177774` (`J11_STKLIM`) is implemented:
  - reset default is `0000400`
  - yellow stack detection uses `J11_STKLIM` (fallback to `0000400` only if register is zero)
- Bus-error trap stacks architectural trap-time PC from `r7`:
  - operand faults use post-fetch PC
  - instruction-fetch faults keep faulting PC
- `core_bus_error_trap(regs *r)` is exported for machine adapters, so external bus/NXM
  traps can reuse the same core trap path.

## Testing (core-level)
From repository root:
- `make test` (MMU-off baseline + core tests)
- `make test-mmu-on` (MMU-on test set)
- `make test-matrix` (both)
- `make diag` (CPU diagnostics harness)

## Primary References
- `doc/J-11_Programmers_Reference_Jan82.pdf`
- `doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf`
- `doc/KM1801VM2.pdf`
- `doc/Однокристальный-микропроцессор-К1801ВМ1.pdf`

## Related Documentation
- Detailed CPU compliance report: [`README.md`](../README.md)
- Emulator/machine integration and devices: [`lsi11/README.md`](../lsi11/README.md)
