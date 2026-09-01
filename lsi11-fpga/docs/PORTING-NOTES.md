# Port history and design decisions

The starting point was synchronous AM4 RTL and recovered MicROM from
`1801BM1/cpu11` commit `e0576637a35f0378f85673213a808098b9535526`. The goal
was to run original LSI-11M before attempting J-11-oriented microcode.

ODT was retained. Legacy DRAM refresh and old Q-bus bootstrap were omitted.
The upstream Wishbone CPU moderator was replaced by direct request/ready.
HC1200's KL11 convention (vectors 060/064), fixed 115200 UART and 50 Hz EVNT
were reused. Guest RAM moved to SPI FRAM; RK registers use private FRAM bank 1.

An early board adapter armed the 50 Hz clock by watching for a nonzero write
to guest vector `000100`. That was a boot workaround rather than a peripheral
model. It was removed in favor of a KW11-L CSR at `177546`: RT-11 owns the
order in which it installs vector `100` and sets interrupt-enable bit 6.

The HC1200 panel register uses `166000/166001`, at the start of DEC's reserved
customer/third-party CSR region. The initially considered `177750/177751`
pair was rejected because `177750` is the processor-owned J-11 MAINT register.
The panel is software-driven to preserve the nearly full MachXO2-1200 target.

Generic 56-bit ROM inference was too expensive. Seven DP8KC blocks now hold
MicROM, and their seven spare bits carry reset bootstrap and private RK code.
A compact extension adds 32 service words. The last physically verified
26.60 MHz fit used 634/640 slices, 1263/1280 LUT4s and 7/7 EBRs.

Guest memory originally used `FRAM_CLK_DIV=2`. The performance revision uses
divider 1 and raises the supported MachXO2 OSCH setting from 26.60 to
29.56 MHz. KW11-L is retuned to an exact 50 Hz divisor and both UART variants
receive the actual clock parameter. SD divisors and protocol are unchanged.

Keeping MR45V100A READ/WRITE transactions open across compatible consecutive
addresses was implemented and simulated. It does not fit this target: the
full form required at least 674/640 slices, and a read-only reduction still
required 672/640. The buildable controller remains one transaction per PDP-11
cycle. A larger FPGA, or removal of an existing board feature, is required for
transparent sequential transactions.

The SD loader was written in DCJ-11 assembly with `microasm11`. Its sequence
was aligned with Stable J11: power delay, 80 clocks, CMD0, CMD8,
CMD55/ACMD41 with CS boundaries, CMD58/CCS, then two CMD17 reads.

The first write service used a register-mediated byte workaround because
original AM4 read the destination of `MOVB memory,memory`. A bus test proved
this CPU semantic issue. MicROM was changed to a true store-only destination
path with correct byte address steps, then the workaround was removed.

The last physical failure occurred after the RT-11 banner. Delay experiments
were a distraction. Stable J11 revealed the difference: it masked SD response
to five bits, while AM4 compared full byte `05`. Real media returned legal
`E5`. Normalizing defined bits fixed that rejection. The subsequent busy wait
was made deterministic by replacing one short hold plus tight polling with six
full 16-bit no-clock intervals and one status sample per interval.

The firmware's eighth bit is not stored in DP8KC: it is a LUT address decoder.
Removing one word from the service moved the sole high-bit literal and changed
logic sharing, producing an unrouteable 643/640-slice build.  The intentional
`NOP` immediately before `command_bytes` restores the resource-proven address
(`boot_addr[4:0] == 18`); the historical adapter then returned to 640 slices.
The current adapter maps to 635 slices. Do not remove or move this packing
spacer without rerunning a clean Diamond implementation.

## Previous verified checkpoint, 2026-08-31

This checkpoint describes the 26.60 MHz pre-performance image, not the current
29.56 MHz source.

- all behavioral AM4 tests including RK READ/WRITE passed;
- Verilator booted RT-11 to monitor prompt;
- Lattice DP8KC simulation passed;
- clean Diamond fit used 634/640 slices, 1263/1280 LUT4s and 7/7 EBRs;
- PAR finished with zero unrouted connections, 5.857 ns setup slack and
  0.304 ns hold slack; TRACE reported zero setup/hold errors;
- Programmer FLASH erase/program/verify succeeded;
- after programming, the physical board responded at the RT-11 prompt;
- physical `DIR` listed 98 files, reported 51455 free blocks and returned to
  the prompt, proving subsequent RK611 reads.

Standalone clean-build JED SHA-256:
`177a852ec916107f94e1c773bd598a48831e99004c203165a850bab33c83427b`;
JEDEC checksum `17FA`.  Rebuild metadata can change the JED identity, so always
regenerate the XCF from the exact JED being programmed.

## Clean implementation checkpoint, 2026-09-01

- board clock 29.56 MHz, FRAM SCK 14.78 MHz (`FRAM_CLK_DIV=1`);
- clean Diamond fit 635/640 slices, 1266/1280 LUT4s and 7/7 EBRs;
- zero unrouted connections, 3.001 ns setup slack and 0.304 ns hold slack;
- behavioral AM4/RK tests and RT-11 boot to monitor prompt passed;
- no SD divider or protocol changes;
- this checkpoint has not yet been programmed or physically verified.

Clean-build JED SHA-256:
`804168220609a3c514377482cd623fb04eb045eafc920bfc1d4c26760630b615`;
JEDEC checksum `BBCA`. The matching generated XCF records file time
`09/01/26 14:11:16`.
