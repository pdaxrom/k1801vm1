# Regression coverage and limitations

| Target | Coverage |
|---|---|
| `am4-direct-test` | MOV/CMP/BNE, 0..3 wait states, stable bus controls, interrupt/RTI |
| `am4-eis-test` | MUL/DIV/ASH/ASHC/XOR results and NZVC edge cases (20 cases) |
| `am4-fis-test` | FADD/FSUB/FMUL/FDIV, pointer rules, PSW, vector 244 and error classes (12 cases) |
| `am4-movb-bus-test` | MOVB modes 1..7, no final destination read, byte steps |
| `am4-fram-test` | FRAM mapping, bytes/words, private RK byte lanes, both UART byte-write decoders, panel GPIO, KW11-L and execution |
| `am4-sd-boot-test` | SD init, CS boundaries, two-sector load, failure path |
| `am4-rk-test` | geometry, low/high physical READ/WRITE, service-ROM-page DMA aliasing, vector 210, absent write-response timeout |
| `am4-odt-test` | retained ODT output through selected UART |
| Python tests | source reproduction, seven EBR lanes, firmware partitions |
| `host-test` | HG protocol/image I/O, restart before export, preservation of invalid existing images |
| `test-vendor` | all MicROM/spare firmware locations through DP8KC model |
| `test-rt11` | complete SD boot to RT-11 monitor prompt |
| project test | direct-bus isolation, board devices, timing-gated Tcl |

The SD write model returns accepted token `E5` with upper undefined bits set.
Backing media is read-only; writes use a 256-sector in-memory overlay.
The normal RK target uses Verilator; `am4-rk-test-icarus` runs the identical
bench more slowly under Icarus.

## Limitations

- The current clean 29.56 MHz build is nearly full: 639/640 slices,
  1271/1280 LUT4s and 7/7 EBRs. Firmware layout affects spare-ROM LUT packing;
  changes require a clean Map/PAR even if the byte size is unchanged.
- SPI FRAM still serializes memory and is slower than BRAM. Divider 1 doubles
  SCK relative to the old board setting, but every access still sends a command
  and 24-bit address. A transparent sequential-read prototype required
  672/640 slices and did not fit this device.
- RK611 is a compatibility subset for RT-11, not cycle-accurate hardware.
- Media must be SDHC/SDXC with 512-byte block addressing.
- The compact write-busy path uses six spaced 16-bit waits but lacks Stable
  J11's full timer/CMD13 sequence.
- HCMS display data, the RGB/keyboard-column shift register and keyboard
  scanning are driven in software through `166000/166001`; there is no
  hardware character framebuffer, font ROM or autonomous scanner.
- Basic-instruction confidence still relies on the recovered upstream MicROM.
  Local directed coverage now includes EIS and FIS, while the full base PDP-11
  instruction/addressing-mode matrix is not exhaustively tested.
- FIS boundary rounding is MicROM-specific; local FIS vectors use exact results
  and cover zero, sign, overflow, underflow and divide-by-zero behavior.
- No MMU is added; this remains the 16-bit LSI-11M memory model.

Any expansion must state which existing resource it replaces. Moving to a
larger FPGA would remove the constraint that motivated spare-bit packing.

## Performance checkpoints

With the same behavioral models, the direct CPU/FRAM smoke test fell from
5556 clocks at 26.60 MHz/divider 2 to 3221 clocks at 29.56 MHz/divider 1:
approximately 209 us to 109 us, or 1.92x faster. Full RT-11 boot to the monitor
prompt fell from 82,651,018 to 53,269,735 clocks; after accounting for clock
frequency, approximately 3.107 s to 1.802 s, or 1.72x faster.
