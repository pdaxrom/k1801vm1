# HC7000 MMU processor events

> Историческая запись: `releases/hc7000-cpu-events/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


CPUERR records HALT/address/bus/stack errors. PIRQ implements seven persistent
software request levels, vector 240, byte-write semantics and external IRQ
arbitration. Kernel-stack protection uses the J11 fixed 0400 limit. Eleven
microinstructions perform red-stack recovery through physical addresses 0/2
and vector 4/6. Opcode fetches from internal CPU/MMU registers raise address
error. User/supervisor trap-stack faults take ordinary bus/MMU exceptions.

The C reference fixes external/PIRQ priority and restricts red-stack fallback
to kernel mode. This release also includes the prior integer corrections:
MFPI, MTPS, RESET restart metadata and TSTSET/WRTLCK with DMA exclusion.
The configurable C STKLIM is an extension: FPGA keeps the architectural fixed
limit and address 177774 remains absent.

Validation passes:

- 2696 differential guest programs, 50599 register/PSW/CSR/memory comparisons,
  using the repository DCJ11 core and Lattice EBR models.
- 233 dedicated CPU-event checks, MMU translation/transactions, ROM, CPU/ODT,
  CSM and locked access with competing DMA; seven C core configurations.
- Nine build-profile tests; HC1200's 47 sources and six generated outputs
  match baseline 9d345ce.
- RTL boots: RT-11 XM (RH0/RH1 plus RK0/RK1/RK2), RT-11 V4 (RK0),
  2.9BSD (RL0/RL1 plus XP0, multiuser root and file writes/readback/sync),
  RSX-11M-PLUS V4.6 BL87 from RQ1 through STARTUP and directory listing.
  The original RQ0 is a nonbootable placeholder; its message and HALT are
  validated separately, not reported as an OS boot.

MAP/PAR/TRACE: **4173/6864 LUT, 1205 FF, 26/26 EBR**, 24 MHz system clock
from the 12 MHz oscillator, routed Fmax **26.263 MHz**. External SRAM timing
constraints pass. CPU events add 130 LUT and 17 FF over the integer-fix release,
with no additional EBR. Microcode uses 1189 words in the same compact ROM.
SERV and its 10515-byte RV32IC firmware are unchanged; peripheral emulation
remains software. Formatting evidence for SERV, both legacy IOP profiles and
HG is preserved in the preceding [integer-fix release](../hc7000-isa-fix/README.md).

The JED, synthesis inputs/reports, source snapshots and validation records
are included, with `SHA256SUMS`. **This JED has not been programmed onto the
board.** Original disk images were not modified. FPP is excluded and this
is not an exhaustive J11 conformance claim. See [CPU events](../../../docs/cpu-events.md)
and [the ISA audit](../../../docs/j11-isa-audit.md) for scope and behavior.
