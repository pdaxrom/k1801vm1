# Frozen comparison inputs

Unmodified copies from the sibling `lsi11-fpga` working tree, inspected on
2026-09-08. Hashes and original paths: `docs/fram-source-audit.json`.
`spi_fram_guest_ram.v` is the baseline transport; `spi_fram_model.v` is the
existing SPI peripheral model. Keeping these snapshots makes remote synthesis
and protocol tests reproducible without changing the working board project.
The new uJ11 transport records its derivation in its source header.

`am4_cpu11_bus.v`, `wbc_uart_xo2.v` and `spi_byte_service.v` are additionally
frozen for the peripheral integration test. They are not synthesized into
uJ11; only the testbench uses this AM4 board-bus implementation. Existing
copyright/source notices are preserved verbatim.
