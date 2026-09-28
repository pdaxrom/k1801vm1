# HC7000 MMU: CPUERR, PIRQ and stack protection

These are processor registers and exceptions, implemented in the MMU CPU.
The peripheral address bridge, disk controllers and SERV firmware are unchanged.
HC1200 and the MMU-less execution engine are unchanged.

## Registers and priority

CPUERR is at physical `17777766`. Bits 7:2 record illegal HALT, address error,
nonexistent memory, I/O timeout, yellow stack and red stack respectively.
Unused bits read zero. Either byte or word writes clear the register;
the RESET instruction leaves it unchanged. Board reset clears it.
The address-error cause also covers opcode fetches from internal CPU/MMU
registers, which must not be decoded as instructions.

PIRQ is at physical `17777772`. Bits 15:9 hold software interrupt requests
for levels 7:1. The highest pending level is encoded in both bits 7:5 and
bits 3:1. Word writes and high-byte writes change requests; low-byte writes
are ignored. RESET clears PIRQ. A granted software request vectors through
`240` and remains pending until cleared by software. It never acknowledges
an external device. The highest unmasked level wins; a software request wins
over an external IRQ at the same level.

The C reference previously polled PIRQ before every external interrupt.
It now polls external sources with an effective mask through the pending
software level, then restores the architectural PSW before saving any frame.
This uses the existing callback contract: polling must respect PSW.IPL and
acknowledges only the selected request. Lower/equal external requests remain
pending. Tests exercise all 49 software/external level pairs and the saved PSW.

## Stack exceptions

J11 protects only the kernel stack, with a fixed virtual limit of octal `0400`.
A checked predecrement/push below that address sets CPUERR.YEL and defers the
vector-4 trap until the instruction completes. Source/destination modes 4/5
using SP, JSR, MFPI/MFPD pushes and trap/interrupt pushes are checked. Arbitrary
arithmetic on SP is not a stack reference. Trace precedes the pending yellow
trap. The stack-trap entry itself does not recursively request another yellow
trap.

An abort during a kernel trap/interrupt stack push sets CPUERR.RED. Eleven
microinstructions at `431`–`43B` (hex) preserve the original PC/PSW in physical
locations `0`/`2`, read the physical vector `4`/`6`, and set kernel SP to zero.
These accesses bypass the failed MMU mapping. Failure of the emergency
sequence enters ODT rather than recursively consuming the stack.

A failed trap push in user or supervisor mode takes the ordinary bus/MMU
exception. It does not set RED or overwrite physical locations `0`/`2`.
The C reference previously applied red-stack recovery to any trap push;
it now checks the current kernel mode, with a dedicated regression test.

There is no programmable STKLIM register on J11. Address `177774` remains an
absent I/O address. The configurable limit in the repository C emulator is
an extension and is not copied into FPGA hardware. The default C limit used
by the differential tests is the architectural `0400`.

The source is the [DEC DCJ11 User's Guide](../../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf),
table 1-8 and sections 1.6–1.8 (printed pages 1-13–1-17; PDF pages 26–30).
The [SIMH J11 register decoder](https://github.com/simh/simh/blob/master/PDP11/pdp11_cpumod.c)
also implements CPUERR/PIRQ but has no STKLIM register for this model.

## Validation and resources

`test_integer_mmu.py` passes **2696 guest programs and 50599 comparisons** with
the real repository core, including CSR readback/byte writes, all PIRQ patterns and IPLs, stack references
in K/S/U modes, trace plus yellow stacking, and MMU failures during trap pushes.
`tb_cpu_events.v` adds a delayed physical bus, external interrupt arbitration,
WAIT/RTT delivery, distinct bus error causes, internal-register opcode fetches,
faults on either trap push, and failure of the emergency stack itself.

The dedicated event bench passes 233 checks. The common regression also passes
786432 MMU translation cases, 3377 transaction checks, 6144 ROM checks,
121 CPU checks, 29 CSM checks and nine locked-bus checks with competing DMA.
All seven C-core configurations pass with MMU enabled/disabled. Nine profile
tests pass; HC1200's 47 sources and six generated outputs match baseline
`9d345ce`.

The board RTL also boots RT-11 XM, RT-11 V4, 2.9BSD and RSX-11M-PLUS V4.6
BL87. BSD reaches multiuser root and verifies RL/RP file writes, readback and
sync. RSX completes STARTUP and lists `DU1:[1,54]RSX11M.SYS` (1026 blocks).
See [boot validation](boot-validation.md) for media and qualification scope.

MAP/PAR/TRACE for `cpu-events-01`: **4173/6864 LUT, 1205 FF, 26/26 EBR**,
24 MHz system clock from the 12 MHz oscillator, routed Fmax **26.263 MHz**.
All external SRAM timing constraints are scored and pass. Compared with
`isa-fix-style-02`, the change adds 130 LUT and 17 FF, with no additional EBR.
The existing compact microstore contains 1189 used words. SERV remains the
same 10515-byte RV32IC firmware.

The source snapshots, checksummed JED, timing reports and validation records
are saved in [the CPU-events release](../releases/hc7000-cpu-events/README.md).

This is RTL/synthesis qualification, not physical-board qualification. It
does not add FPP or claim exhaustive conformance to every J11 corner case.
