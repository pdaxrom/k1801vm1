# lsi11-fpga TODO

## HC1200 AM4 hardware panel shifter (2026-08-31)

Goal: replace bit-banged HCMS/74HC595 pin transitions with byte-level hardware
transfers without moving the design to a larger FPGA.

Chosen design:
- [x] Validate feasibility by reusing the existing SD `spi_byte_service` for
  panel writes. A separate byte shifter does not fit in LCMXO2-1200HC.
- [ ] Extend the private panel register block: keep `166000/166001` as the
  control/keyboard CSR and add a write-only byte-transfer register at `166002`.
- [ ] Define CSR high-byte bits 0..2 as HCMS `CE`, `RS`, and `BLANK`, and bit 3
  as the transfer target (`0` = HCMS, `1` = external 74HC595).
- [ ] Stall the PDP-11 bus write to `166002` until eight MSB-first bits have
  been shifted. Force SD CS high during a panel transfer and gate the panel
  clock off during an SD transfer.
- [ ] Keep HCMS CE under software control across a multi-byte transaction:
  lower CE, write all command/display bytes, then raise CE. A complete
  16-character display refresh is 80 bytes.
- [ ] For a 74HC595 transfer, require HCMS CE high and generate one registered,
  full-FPGA-clock `REG_LATCH` pulse after the eighth bit. Preserve the existing
  byte layout: bits 0..2 are active-low RGB and bits 3..7 select the five
  keyboard columns.
- [ ] Keep keyboard scanning in software: write one column/RGB byte, wait for
  the hardware transfer and latch, then read the four keyboard rows from
  `166000` bits 7..4. Add debounce in software if required.
- [ ] Force panel transfers through the existing slow divider (about 196 kHz,
  41 us/byte and 3.3 ms for 80 bytes). Keep SD/RK611 at its current fast rate.
  The shared fast rate is about 6.65 MHz and exceeds the HCMS-3917 4 MHz limit
  at 3 V.
- [ ] Update `docs/PANEL-IO.md`, architecture/testing documentation and PDP-11
  driver examples for the new byte-level interface.
- [ ] Add regression coverage for HCMS byte order and CE behavior, SD-CS
  isolation, automatic 74HC595 latch, RGB/column preservation and keyboard-row
  readback.
- [ ] Run the complete simulation suite, Diamond Map/PAR/bitgen, program the
  HC1200 and verify HCMS output, RGB colors, all 20 keys, SD boot and RK611 I/O.

Feasibility measurements from the isolated prototype:

| Variant | Slices | LUT4 | Result |
|---|---:|---:|---|
| Current AM4 configuration | 634/640 | 1263/1280 | fits |
| Separate panel byte shifter | 663/640 | 1317/1280 | does not fit |
| Separate shifter plus autonomous keyboard scanner | 682/640 | 1355/1280 | does not fit |
| Shared SD/panel shifter with safe panel divider | 636/640 | 1265/1280 | fits |

The passing prototype uses 468 registers and all 7 EBRs. Diamond completed
with zero unrouted connections, setup slack 5.372 ns, hold slack 0.304 ns and
no timing errors. The focused panel test, the fast suite (including SD boot and
ODT), and the RK611 READ/WRITE test passed. Only four slices and fifteen LUT4s
remain, so an autonomous keyboard scanner, RGB PWM, HCMS framebuffer/font ROM,
or another independent SPI engine should not be added to this configuration.
