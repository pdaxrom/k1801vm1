# HC7000 SERV compaction

> Историческая запись: `releases/hc7000-serv-compact/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


Validated intermediate configuration before adding RK05/MSCP. Source baseline
is `1e5aeed`; the four changed implementation/test files are preserved under
`source/`. This package contains firmware and synthesis/test evidence, not a
programmed-board qualification or a new SD image.

| Metric | Previous BSD build | This build |
|---|---:|---:|
| SERV ISA | RV32I | RV32IC |
| Firmware code | 6320 bytes | 4800 bytes |
| Static data | 796 bytes | 796 bytes |
| Stack reservation | 1024 bytes | 1024 bytes |
| Space before reserved stack | 52 bytes | 1572 bytes |
| Total EBR | 25/26 | 22/26 |
| LUT4 | 5435/6864 | 5698/6864 |
| Clock | 24 MHz | 24 MHz |
| Routed Fmax | 25.539 MHz | 24.592 MHz |

RV32IC keeps 32-bit registers/address arithmetic and mixes 16- and 32-bit
instructions. The generated firmware RAM file also selects the matching SERV
decoder, so cached firmware and hardware cannot silently disagree about ISA.
Only `IOP=storage` selects compression; legacy firmware remains RV32I, 1188 bytes.

The no-FPP MMU microstore keeps all logical addresses and 54-bit words. Its dense
first 1024 words use six EBRs; the sparse tail uses three 512x18 EBRs with a checked
address fold. Every logical address and clock-enable hold was compared against
the original microcode image using Lattice EBR models. MMU-less/HC1200 ROM
generation and the full-FPP layout are unchanged. All 2 MiB guest SRAM remain
available.

Validation includes RH, RL/XP with generic and vendor RAM, legacy SERV, all 47
HC1200 source hashes, RT-11 XM on RH0/RH1, and 2.9BSD through multiuser/root with
RL/XP reads and writes. BSD passed 18943 checks in 1137676676 clocks. The recorded
OS matrix still documents the existing unsupported RK05/RQ cases; it does not
claim those controllers work in this intermediate build.

The board continues to run the previously installed BSD release. No board flash
or physical SD writes were performed for this compaction.
