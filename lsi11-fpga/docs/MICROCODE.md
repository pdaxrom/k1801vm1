# MicROM source and modifications

## Recovered source

`ucode/experimental/am4/mc.asm` is a symbolic reconstruction of the original
1024 x 56 AM4 control store. It uses `tools/meta29.py` and
`tools/am29_m4.def`. The normal build:

1. assembles `mc.asm` into `build/mc.rom`;
2. compares it byte-for-byte with checked-in patched `mc.rom`;
3. compares that result with `upstream/mc-original.rom`;
4. requires the difference to be exactly the documented 18 addresses.

Run this explicitly with `make microcode-check`. `mc.rom` is reproducible
generated data and a simulation input, never the file edited by hand.

## Why MOVB was changed

The original destination effective-address path treats a memory destination
like a read/modify/write operand. For `MOVB memory,memory`, it starts a read at
the final destination before the byte write.

That is invisible in ordinary RAM and incorrect for a read-sensitive device.
At the SD data register every read clocks a byte, so
`movb (r2)+,(r4)` produced `data, FF, data, FF, ...` and `CMD24` failed.

The MicROM fix gives MOVB/MFPS a store-only destination path:

- resolve deferred pointers and index words;
- finish any pointer-read `SYNC` phase;
- latch the final destination without reading its operand;
- choose the byte lane after the address is known;
- perform one byte write;
- preserve PDP-11 steps: R0-R5 use one for bytes, SP/PC use two.

`tb_am4_movb_bus.v` covers destination modes 1 through 7 and requires zero
reads of every final destination.

## Modified addresses

| Address | Purpose |
|---|---|
| `001`, `002` | enter store-only EA and close any pointer read |
| `0B2`, `0B4` | byte-aware mode 2 and mode 4 address updates |
| `290` | relocate original SEX helper call |
| `29F` | fix board boot entry and free old boot-selector OR function |
| `308`, `309` | latch store-only address and select byte lane |
| `30D` | retire old boot-selector word |
| `30E`, `30F` | byte autodecrement dispatch and R0-R5 compensation |
| `319` | relocated original SEX helper |
| `31A`, `31B` | mode-2 word/byte increment dispatch |
| `31C`, `31D` | mode-4 word/byte dispatch |
| `31E`, `31F` | SP/PC `+2` and R0-R5 `+1` byte increment |

Exact before/after 56-bit values are printed by `make microcode-check`.

## Supporting RTL selectors

`am4_direct.v` changes two OR-multiplexer interpretations:

- selector 4 becomes `OR_BF`, driven by the byte flag;
- the signature at `309` becomes `OR_AP`, selecting destination address parity.

`OR_AP` is qualified by the microinstruction signature because its selector
pattern occurs in unmodified words. A global reinterpretation would alter
unrelated microcode.

## Editing procedure

1. Edit `mc.asm`, never hexadecimal ROM.
2. Run `make microcode-check`.
3. Update `EXPECTED_PATCHES` only for an intentional documented address.
4. Run `make test-fast`, especially the transaction-counting bus test.
5. Run `make test`, `make test-rt11`, `make test-vendor`, and `make diamond`.
6. Recheck resources before programming; the accepted build has no spare EBR
   and no spare slices.
