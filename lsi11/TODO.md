# LSI11 TODO

## Host-Side Disk Tools

### rt11tool

- [ ] Extend `fsck --repair` beyond current directory/EOS checks; add safer recovery for damaged directory layout.
- [ ] Interpret RT-11 extra directory words instead of only preserving them.
- [ ] Add wildcard selection for `ls`, `extract`, and `rm`.
- [ ] Improve boot/partition diagnostics beyond current raw bootblock and partition selection support.

### rsx11tool

- [ ] Add lost-file recovery / salvage flows beyond current orphan cleanup.
- [ ] Detect cross-linked or overlapping extents and define a safe repair policy for them.
- [ ] Repair or rebuild missing file headers and damaged `MFD`/`UFD` structures.
- [ ] Generate RSX-11 bootstrap during `mkfs` instead of only copying raw block 0.
- [ ] Extend directory management beyond `UFD`-only `mkdir`/`rmdir`.
- [ ] Add an `ODS-2` backend as a separate scope item.
