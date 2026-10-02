# HC7000 shared SERV peripheral interface

> Историческая запись: `releases/hc7000-serv-io/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


The MMU storage profile sends peripheral bus cycles to one SERV mailbox.
RH11, RL11, XP/RP, RK05 and RQ/MSCP register models, command state and interrupt
selection execute in C. The old storage-profile RTL register banks are removed;
the legacy RK611 bank remains only for `IOP=legacy`.

The FPGA retains the mailbox handshake, shared SPI/sector DMA engine, UART,
clock, HG pins and UNIBUS translator. Adding another software controller does
not require another FPGA register bank. Ordinary CPU memory accesses continue
independently; a peripheral access waits for the SERV response or NXM.

| Metric | Committed compact build | Shared I/O build |
|---|---:|---:|
| LUT4 | 5698 / 6864 | 4014 / 6864 |
| Flip-flops | 2157 | 1187 |
| EBR | 22 / 26 | 26 / 26 |
| SERV program | 4800 bytes | 10515 bytes |
| SERV RAM | 8 KiB | 12 KiB |
| System clock | 24 MHz | 24 MHz |
| Routed Fmax | 24.592 MHz | 26.087 MHz |

Compared with the rejected RK05/MSCP hardware-register prototype (6564 LUT),
the shared interface frees 2550 LUT. EBR remains fully occupied: controller
firmware now uses twelve EBRs, the no-FPP microstore nine, and other memories
five. This change frees logic, not block RAM. All 2 MiB external SRAM remain
available to PDP-11 software.

SERV uses RV32IC, link-time optimization and shared save/restore routines.
Controller registers use 16-bit storage; drive masks use bytes. Startup label
scratch is reused for runtime registers and MSCP packets. The linker reserves
768 bytes for the stack; integration tests observed at most 320 bytes and check
every SERV RAM write against the code, BSS and stack boundaries.

RSX startup requires CSM (change to supervisor mode). The MMU profile now
implements its stack frame and mode transition in microcode, including separate
supervisor I/D spaces and stack-write aborts. The 31 added microinstructions fit
in the existing nine microstore EBRs. CSM also needs CPU instruction dispatch and
supervisor-space selection in RTL; the complete synthesized design grows by
29 LUT compared with the same shared-I/O build without CSM (3985 to 4014).
MMU-less CPU decoding and microcode are unchanged.

Full RTL boots use the final firmware, CPU microcode, SPI SD model, external
SRAM pins and sampled UART waveforms. RT-11 XM boots RH0 and reads RH1 plus
RK0/RK1/RK2; RT-11 V4 boots and reads RK0. 2.9BSD boots RL0, mounts XP0's
`/usr`, enters multiuser mode and verifies file writes/readback on RL and RP.
The BSD run passes 22108 checks in 1305665221 clocks with 421632 DMA words.
RSX-11M-PLUS V4.6 BL87 boots RQ1, completes STARTUP, reports DU0/DU1 and reads
the system-file directory (3346 checks, 1637690453 clocks, 786149 DMA words).
The original RQ0 image remains a nonbootable placeholder, checked separately
against its message and HALT PC 000034. No source disk was altered.

The [J11 ISA audit](../../../docs/j11-isa-audit.md) compares all 65536 opcode
dispatches with the instruction families in `core/core.c`. TSTSET/WRTLCK remain
unimplemented in both profiles; five mode/space instructions are intentionally
excluded from mmuless. The audit also records semantic differences, including
MFPI user-to-user space selection and MTPS privilege handling. This peripheral
change does not claim to resolve those CPU differences.

Source inputs and MAP/PAR/TRACE reports are under `synthesis/`; firmware, menu
and test evidence have their own directories. Sources used by synthesis match
the preserved hashes. The menu assembler was built from the repository's pinned
`microasm11` source, leaving the user's modified submodule untouched.

HC1200's 47 source hashes and six generated outputs match the saved baseline.
Legacy SERV remains RV32I with the identical 1188-byte firmware and passes its
disk test in an isolated build directory. Board programming and physical SD
writes were not performed for this change.
