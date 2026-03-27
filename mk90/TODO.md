# MK-90 TODO

- [ ] Implement the `MK-92` / `VG6` external-ROM path used by ROMT tests `4..6`.
  Model the presence/status register at `0164036` (`0xE81E`) and the page/control
  register at `0164200` (`0xE880`), then map the selected 16 KiB bank into
  `040000-077777`.
- [ ] Re-run ROMT tests `4`, `5`, and `6` after `VG6` exists and pin them in
  smoke/regression coverage.
