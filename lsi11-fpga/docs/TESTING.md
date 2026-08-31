# Regression coverage and limitations

| Target | Coverage |
|---|---|
| `am4-direct-test` | MOV/CMP/BNE, 0..3 wait states, stable bus controls, interrupt/RTI |
| `am4-eis-test` | MUL/DIV/ASH/ASHC/XOR results and NZVC edge cases (20 cases) |
| `am4-fis-test` | FADD/FSUB/FMUL/FDIV, pointer rules, PSW, vector 244 and error classes (12 cases) |
| `am4-movb-bus-test` | MOVB modes 1..7, no final destination read, byte steps |
| `am4-fram-test` | FRAM mapping, bytes/words, private RK bank, execution |
| `am4-sd-boot-test` | SD init, CS boundaries, two-sector load, failure path |
| `am4-rk-test` | geometry, three reads, one sector write, vector 210, integrity |
| `am4-odt-test` | retained ODT output through selected UART |
| Python tests | source reproduction, seven EBR lanes, firmware partitions |
| `test-vendor` | all MicROM/spare firmware locations through DP8KC model |
| `test-rt11` | complete SD boot to RT-11 monitor prompt |
| project test | direct-bus isolation, board devices, timing-gated Tcl |

The SD write model returns accepted token `E5` with upper undefined bits set.
Backing media is read-only; writes use a 256-sector in-memory overlay.
The normal RK target uses Verilator; `am4-rk-test-icarus` runs the identical
bench more slowly under Icarus.

## Limitations

- MachXO2-1200HC is full: 640/640 slices, 1272/1280 LUT4s and 7/7 EBRs.
  The `NOP` before `command_bytes` is an intentional spare-ROM packing spacer;
  removing it was measured at 643 slices and failed Map.
- SPI FRAM serializes memory and is much slower than BRAM.
- RK611 is a compatibility subset for RT-11, not cycle-accurate hardware.
- Media must be SDHC/SDXC with 512-byte block addressing.
- The compact write-busy path uses six spaced 16-bit waits but lacks Stable
  J11's full timer/CMD13 sequence.
- Timer starts only after a nonzero vector-100 handler is installed.
- Basic-instruction confidence still relies on the recovered upstream MicROM.
  Local directed coverage now includes EIS and FIS, while the full base PDP-11
  instruction/addressing-mode matrix is not exhaustively tested.
- FIS boundary rounding is MicROM-specific; local FIS vectors use exact results
  and cover zero, sign, overflow, underflow and divide-by-zero behavior.
- No MMU is added; this remains the 16-bit LSI-11M memory model.

Any expansion must state which existing resource it replaces. Moving to a
larger FPGA would remove the constraint that motivated spare-bit packing.
