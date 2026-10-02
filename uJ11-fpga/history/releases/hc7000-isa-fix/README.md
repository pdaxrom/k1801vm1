# HC7000 MMU integer corrections

> Историческая запись: `releases/hc7000-isa-fix/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


MFPI user-to-user uses UD, nonkernel MTPS preserves IPL, and RESET preserves
MMR1/MMR2 until the next instruction fetch. TSTSET/WRTLCK use ten previously
free microinstructions. A one-bit CPU lock prevents SRAM DMA from interleaving
TSTSET read/write cycles and releases on completion or bus/MMU abort.

The DCJ11 C reference is corrected to take trap current mode from the vector
(DEC UG table 1-4) and to clear unused PSW bits 10:9 (figure 1-3). The existing
FPGA trap behavior is retained, including the RT-11 BASIC error-handler fix.

The differential test executes 1382 guest programs and compares 23328 register,
PSW and memory values with the repository core, using Lattice EBR simulation.
MMU/CSM/ODT/ROM, concurrent DMA and the core MMU-on/off regression matrix pass.
RTL boots cover RT-11 XM (RH0/RH1 + RK0/RK1/RK2), RT-11 V4 (RK0), 2.9BSD
(RL0/RL1 + XP0, multiuser root and file write/readback), and RSX-11M-PLUS (RQ1).
Original RQ0 remains a nonbootable placeholder; its message/HALT are checked.

MAP/PAR/TRACE: **4043/6864 LUT, 1188 FF, 26/26 EBR**, 24 MHz system clock,
12 MHz oscillator, routed Fmax 25.662 MHz. Compared with the prior SERV I/O
release this adds 29 LUT and one FF, with no additional EBR. SERV firmware is
unchanged. SERV C/header sources were formatted with the user's astyle command;
`validation/serv-format.json` proves the rebuilt firmware and both generated
memory modules match the payloads used by the OS tests byte for byte. The
post-format synthesis and differential/core tests use current source hashes.
Both legacy IOP profiles and the HG host sources use the same formatting.
`validation/style-extra/` verifies identical 1168/1188-byte IOP binaries,
generated ROM contents, and passing HG protocol/service/calendar/MPSSE tests.
HC1200's 47 source hashes and six generated outputs match baseline.

FPP is unchanged and excluded from this task. CPUERR/PIRQ/STKLIM and yellow/red
stack handling remain a separate CPU architecture step. The mmuless profile is
unchanged, including its unsupported TSTSET/WRTLCK. This is not a claim of full
J11 conformance.

The JED is included with synthesis inputs/reports and checksum verification.
**This build has not been programmed onto the board.** Source disk images were
not modified. See [the ISA audit](../../../docs/j11-isa-audit.md) for details.
