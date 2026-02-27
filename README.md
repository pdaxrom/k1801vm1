# DCJ11 CPU Core — Compliance Report

This report documents DCJ11 (J‑11) CPU core behavior implemented in `core/`
and verified by tests in `tests/`. All PDP‑11 values below are in OCTAL.

---

## 1. Scope and Methodology

### 1.1 Scope
CPU semantics only:
- instruction execution
- PSW and flags
- traps/exceptions
- interrupt acceptance/priority
- DCJ11‑specific SEL registers/signals (SEL0/SEL1/SEL2)

Machine‑specific behavior (bus timing, devices, DMA, peripherals) is out of scope.

### 1.2 Verification Method
- static inspection of `core/`
- unit/diagnostic tests in `tests/core_tests.c`
- cross‑checking against documentation listed below

Each item is marked:
- PASS — matches documented behavior (within scope)
- FIXED — previously wrong, corrected here
- LIMITED — partially implemented or stubbed
- NOT IMPLEMENTED — intentionally missing

---

## 2. Documentation References

Primary DCJ11/J‑11 references (DEC):
- `doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf` (DCJ11 User Guide)
- `doc/J-11_Programmers_Reference_Jan82.pdf` (J‑11 Programmer’s Reference)
- `doc/EK-KDJ1B-UG_KDJ11-B_Nov86.pdf` (KDJ11‑B CPU module guide)

SEL signal/register behavior cross‑checked with USSR‑compatible CPU docs:
- `doc/Однокристальный-микропроцессор-К1801ВМ1.pdf` and `doc/1801vm.txt`
- `doc/KM1801VM2.pdf`

---

## 3. CPU State and PSW

### 3.1 General Registers
R0–R7: **PASS**
- Verified by instruction tests and operand decode tests.

### 3.2 PSW
N/Z/V/C, T, and priority bits: **PASS / FIXED**
- N/Z/V/C and T verified by tests (TSTB, trace, RTT/RTI).
- Priority masking verified by DCJ11 IRQ tests.
- Memory-mapped PSW register access at `0177776` is handled in `core/`
  (CPU-level), not in machine device layers.

Mode bits: **LIMITED**
- With `ENABLE_MMU=0` (default), memory is identity-mapped and mode bits do not
  affect address translation.
- With `ENABLE_MMU=1`, mode bits select Kernel/Supervisor/User PAR/PDR sets.

### 3.3 CIS / PSW<8> Policy
- CIS (Commercial Instruction Set / DIS option) is not implemented in this emulator.
- For `DCJ11`, `PSW<8>` is not modeled as "CIS instruction suspended".
- For `VM1/VM2`, `PSW<8>` keeps K1801 meaning (`H/U`, HALT/USER bank/mode), not CIS.
- Internal `FLAG_H` naming in shared code must not be interpreted as CIS support.

References: J‑11 Programmer’s Reference (PSW and trap/interrupt behavior).

---

## 4. Stack Frame Format

### 4.1 Trap / Interrupt Entry
Stack frame (top → bottom):
- PC
- PSW

Status: **FIXED**
- Correct push order validated by trap/interrupt tests.

### 4.2 RTI Return Semantics
Restore order:
1) PC
2) PSW

Status: **FIXED**
- Verified by RTI tests.

References: J‑11 Programmer’s Reference (trap/interrupt entry, RTI/RTT).

---

## 5. Trap and Exception Vectors

| Trap / Exception        | Vector | Priority | Status |
|-------------------------|--------|----------|--------|
| Bus Error               | 000004 | 7        | PASS   |
| Reserved Instruction    | 000010 | 7        | PASS   |
| BPT                     | 000014 | 7        | PASS   |
| IOT                     | 000020 | 7        | PASS   |
| EMT                     | 000030 | 7        | PASS   |
| TRAP                    | 000034 | 7        | PASS   |
| Trace                   | 000014 | 7        | PASS   |

References: J‑11 Programmer’s Reference (trap vectors).

---

## 6. Interrupt System

### 6.1 Acceptance Rules
- Interrupts accepted at instruction boundaries: **PASS**
- Priority masking rule (IRQ priority > PSW.priority): **FIXED**

### 6.2 Multiple Pending Interrupts
Selection rule: highest priority first
- **PASS**
- Verified by a synthetic poller that exposes multiple pending priorities.

### 6.3 WAIT Instruction
- Halts instruction fetch
- Resumes only on interrupt

Status: **FIXED**

References: J‑11 Programmer’s Reference (interrupt system, WAIT behavior).

---

## 7. Instruction Semantics (Relevant Subset)

### 7.1 Byte Instructions
`TSTB`:
- N ← bit 7 of byte
- Z ← byte == 0
- V ← 0

Status: **FIXED**

### 7.2 Branch Instructions
`BPL`:
- branch if N == 0
- offset is in words

Status: **PASS**

### 7.3 Privileged Base Instructions (DCJ11/VM2)
`SPL n` (`0000230..0000237`):
- kernel mode: sets `PS<7:5>` to `n`
- supervisor/user mode: treated as NOP
- supported on DCJ11/VM2/K1806VM2
- VM1: illegal-instruction trap

`MFPS dst`:
- transfers low byte of `PSW` as byte operation (`MOVB`-style destination semantics)
- register destination gets sign-extended byte
- memory destination performs byte store and byte autoincrement rules
- condition codes: `N/Z` from transferred byte, `V=0`, `C` unchanged

`MTPS src`:
- updates low PSW byte mask as implemented in core
- on DCJ11 in supervisor/user mode, `PS<7:5>` is preserved

`TSTSET dst`, `WRTLCK dst`:
- implemented for DCJ11 only
- non-DCJ11 models trap as illegal instruction

`MFPD dst`, `MFPI dst`, `MTPI dst`, `MTPD dst`:
- implemented on DCJ11 and VM2/K1806VM2
- VM1 trap as illegal instruction

Status: **FIXED / PASS**

### 7.4 FIS Instructions
`FADD Rn`, `FSUB Rn`, `FMUL Rn`, `FDIV Rn` (`075000..075037`):
- Implemented natively using IEEE-754 `float` operations in software.
- Converts to and from DEC single-precision F-format dynamically.
- `Rn` and `4(Rn)` point to the floating-point operands on the PDP-11 stack/memory.
- Generates FIS error trap (vector 000244) and pushes error code in place of PSW on:
  - Overflow (error code 02)
  - Underflow (error code 012)
  - Division by zero (error code 013)
- Standard FIS condition code semantics (`N` = sign, `Z` = zero, `V` = 0, `C` = 0).

Status: **PASS / FIXED**

References: J‑11 Programmer’s Reference (instruction set).

---

## 8. DCJ11‑Specific Features (SEL Registers / Signals)

### 8.1 Identification
SEL‑related entities implemented in `core/`:
- SEL0 — unaddressed SEL register (CPU‑internal)
- SEL1 — external register at 0177716
- SEL2 — external register at 0177714
- DCJ11 CPU register at `0177750` (core-owned register, reset `0`)

### 8.2 Semantics
Implemented behavior:
- Reset PC uses SEL0 high byte: PC ← (SEL0 & 0177400), PSW ← 000340
- RSEL reads SEL0 into R0 (VM2 only)
- SEL1/SEL2 are memory‑mapped external registers; byte and word access supported
- SEL1/SEL2 are handled by the hardware stub (`core/hardware.c`)

Status: **FIXED / LIMITED**
- CPU‑visible semantics are implemented and tested.
- External bus signaling (SEL pin timing, DIN/DOUT/RPLY handshake) is not modeled.

References:
- `doc/1801vm.txt` (procedure of initial start; SEL‑based start vector)
- `doc/KM1801VM2.pdf` (SEL signal description; HALT/SEL behavior)

---

## 9. Differences vs K1801VM1 (Summary)

Key differences (within this project’s scope):
- DCJ11 treats VM2 privileged instructions (GO/STEP/RSEL/RCPC/RCPS/WCPC/WCPS) as illegal
- DCJ11 interrupt priority masking behavior follows J‑11 rules

Impact:
- VM1 software expecting SEL behavior still works at the CPU level; device‑level
  SEL signaling is not emulated.

References: `doc/1801vm.txt`, `doc/KM1801VM2.pdf`.

---

## 10. Differences vs K1801VM2 (Summary)

Key differences (within scope):
- MMU is compile-time optional (`ENABLE_MMU`, default off)
- HALT/SEL vectoring follows implemented DCJ11 rules (SEL0)

Impact:
- With default build (`ENABLE_MMU=0`), supervisor/user memory distinctions are not enforced.
- With MMU build (`ENABLE_MMU=1`), K/S/U relocation/protection and split I/D are enforced.

References: `doc/KM1801VM2.pdf`.

---

## 11. K1801VM1 Compliance (CPU Core)

This section summarizes VM1 behavior implemented in `core/` and verified
by tests. All values are OCTAL.

References:
- `doc/Однокристальный-микропроцессор-К1801ВМ1.pdf`
- `doc/1801vm1.txt`

### 11.1 VM1 Trap/Exception Entry
Entry stack frame (top → bottom):
- PC
- PSW

Status: **PASS**

### 11.2 VM1 RTI Semantics
RTI restores:
1) PC
2) PSW

Status: **PASS**

### 11.3 VM1 HALT Semantics (Pult Exception)
HALT generates a “pult” exception:
- PSW saved at 0177676
- PC saved at 0177674
- bit 010 set in 0177716
- PC loaded from 0160002, PSW loaded from 0160004

Status: **PASS / FIXED**

### 11.4 VM1 WAIT Semantics
- WAIT sets fWait and stops instruction fetch.
- T‑bit does not force an immediate trace exit while waiting.

Status: **PASS / FIXED**

### 11.5 VM1 Interrupt Masking
- PSW7 (bit 7, P) masks IRQ2/IRQ3/VIRQ.
- PSW10 (bit 10) masks IRQ1/IRQ2/IRQ3/VIRQ.
- PSW11 (bit 11) masks IRQ1.
- **Fixed IRQ reliability**: interrupts are now acknowledged (`ACK`) only when the CPU core is ready to accept them, preventing lost interrupts during high-priority tasks.

Status: **PASS / FIXED**

### 11.6 VM1 Multiple Interrupt Selection
- When multiple IRQs are pending, IRQ1 has highest priority over TVE, IRQ2, IRQ3, VIRQ.
- VM1 accepts full vectors (no 9‑bit priority encoding on the bus).

Status: **PASS / FIXED**

### 11.7 VM1 Timer Registers (VE1)
- TVE_LIMIT at 0177706 (R/W)
- TVE_COUNT at 0177710 (RO)
- TVE_CSR at 0177712 (R/W, high byte reads as 0377)
- Writes to TVE_CSR copy TVE_LIMIT → TVE_COUNT

Status: **PASS / LIMITED**

### 11.8 VM1G Notes
- VM1G profile support has been removed.

---

## 13. K1801VM2 Compliance (CPU Core)

This section summarizes VM2 behavior implemented in `core/` and verified
by tests. All values are OCTAL.

References:
- `doc/KM1801VM2.pdf`

### 13.1 VM2 Mode and Copy Registers
- VM2 mode is selected only by H/U bit `0000400`:
  - `H/U=0` USER
  - `H/U=1` HALT
- CPC/CPSW track PC/PSW unless both P and H/U bits are set.
- When P=1 and H/U=1, CPC/CPSW are locked for debugger/FIS use.

Status: **PASS / FIXED**

### 13.2 VM2 HALT Instruction / HALT Signal
- HALT vectors via SEL170 (SEL0 high byte + 0170).
- External HALT is masked when H/U=1.
- After STEP, HALT is deferred until one instruction executes.

Status: **PASS / FIXED**

### 13.3 VM2 HALT‑Mode Instructions
- START/STEP, RCPC/RCPS, WCPC/WCPS require H=1 and P=1.
- RSEL/MFUS/MTUS require H=1.

Status: **PASS / FIXED**

### 13.4 START vs STEP Semantics
- START loads PC/CPSW from CPC/CPSW and immediately checks interrupts.
- STEP loads PC/CPSW from CPC/CPSW and does **not** start interrupt processing.

Status: **PASS / FIXED**

### 13.5 FIS Trap Handling (VM2)
- FIS opcodes 075000–075037 trap to SEL010 (SEL0 high byte + 0010).
- If SEL0 bit 7 is set, FIS traps as illegal (vector 000010).
- FIS error trap uses vector 000244, pushing the appropriate error code (02, 012, 013) to the stack instead of PSW.

Status: **PASS / FIXED**

### 13.6 VM2 RTI H/U Semantics
- H/U is restored only when new PC ≥ 160000; otherwise H/U preserved.

Status: **PASS / FIXED**

### 13.7 VM2 MTPS Masking
- `MTPS` updates only `PSW[7:5]` and `PSW[3:0]` (`0000340 | 0000017`).
- `PSW` bit `0000020` (T) is preserved.
- `PSW` bit `0000400` (H/U) is preserved.

Status: **PASS / FIXED**

### 13.8 VM2 Trap/IRQ Vector PSW Load
- From USER (`H/U=0`): vector load uses `PSW[7:0]` and forces `H/U=0`.
- From HALT (`H/U=1`): vector load uses `PSW[8:0]`, so vector may set/clear `H/U`.
- VM2 vector entry path does not use DCJ11 CM/PM stack-mode logic.

Status: **PASS / FIXED**

### 13.9 VM2 IRQ Masking with `P` Bit
- `PSW.P=1` (bit 7) masks VM2 external interrupts `EVNT` and `VIRQ`.
- Interrupts are only acknowledged (`ACK`) when they can be accepted by the CPU.

Status: **PASS / FIXED**

### 13.10 VM2 Address Space Banking
- VM2 supports two independent 64KB banks: USER and HALT.
- Bank selection is controlled by `PSW.H/U` (bit 8).
- Implementation uses 17-bit physical addresses in core hardware stub (`addr | 0200000` for HALT mode).

Status: **PASS / FIXED**

---

## 14. K1806VM2 Alias

K1806VM2 is treated as an alias of K1801VM2 in this core. All VM2 tests are
run for both model IDs.

Status: **PASS**

---

## 15. Diagnostic Tests

Implemented tests (see `tests/core_tests.c`):
- trap/interrupt stack frame order
- RTI restore order
- WAIT + interrupt resume
- IRQ priority masking and highest‑priority selection
- VM2 masked IRQ retention until unmask (`vm2_irq_mask_hold`)
- TSTB flags and BPL branching
- DCJ11 SEL0/SEL1/SEL2 semantics
- `SPL` kernel/user semantics on DCJ11/VM2 and illegal trap on VM1
- `MFPD/MFPI/MTPI/MTPD` availability on DCJ11/VM2 vs VM1
- `MFPS` byte semantics (register sign-extension, memory byte-store/autoincrement)

Run:
```
make test
```

MMU test matrix:
```
make test-matrix
```
Includes MMU-on tests for register decode, faults, split I/D and 22-bit physical
memory fetch/store paths.

---

## 16. Known Limitations

- With `ENABLE_MMU=1`, core hardware stub provides 22-bit physical backing
  memory (4MB) so MMU translation above `0177777` is testable in unit tests.
- With `ENABLE_MMU=0`, hardware stub uses legacy 64KB memory backing by default,
  expanded to 128KB when VM2 model is active to support USER/HALT banking.
- Machine-specific frontends may still provide their own bus/memory limits.
- `SSR1` is implemented as a simplified fault context latch (fault VA), not a complete
  per-microstep register modification log.
- External bus timing and SEL/DIN/DOUT/RPLY signal timing are not modeled.
- Device interrupt arbitration is outside the core (handled by machine layer).
- Vector-fetch/stack-push aborts follow recoverable abort semantics (restoring pre-trap
  `PC/PS`); a dedicated hardware "hang latch" state is intentionally not modeled.

---

## 17. Conclusion

Overall DCJ11 compliance status: **MOSTLY COMPLIANT** for CPU‑level semantics.
Core behavior matches documented trap/interrupt rules and tested instruction
semantics, includes optional MMU support behind `ENABLE_MMU`, with explicit
limitations around 22-bit physical memory modeling and device‑level bus signaling.

---

## 18. DCJ11 MMU (ENABLE_MMU)

Build-time switch:
- `ENABLE_MMU=0` (default): MMU translation disabled, no MMU faults.
- `ENABLE_MMU=1`: MMU logic compiled in; runtime enable via `SSR0` bit 0.

Implemented register map (OCTAL):
- `SSR0..SSR3`: `177572`, `177574`, `177576`, `177516`
- Supervisor PAR/PDR: `172200..172276`
- Kernel PAR/PDR: `172300..172376`
- User PAR/PDR: `177600..177676`

Supported MMU behavior (`ENABLE_MMU=1`):
- K/S/U mode selection from PSW.
- 8 segments per space, PAR/PDR relocation.
- Split I/D per mode via `SSR3` bits `KD/SD/UD`.
- `SSR3` bit `BME` (`040`) is latched/readable, but Unibus map translation is not implemented.
- Instruction fetch in I-space, data in D-space when split enabled.
- Trap/interrupt vector fetch through Kernel D-space (when split I/D enabled) or I-space.
- Fault classes: non-resident, length, write-protect; trap vector `000250`.
- `SSR0` fault latch + runtime enable bit, `SSR2` fault PC, `SSR1` simplified VA latch.

`ENABLE_MMU=0` policy:
- MMU registers are present for DCJ11 accesses, read as `0`, writes ignored.

MMU compliance table:

| Feature | Implemented | Notes |
|--------|-------------|-------|
| Compile-time MMU switch (`ENABLE_MMU`) | YES | Default off |
| Runtime MMU enable (`SSR0` bit 0) | YES | Translation active only when set |
| K/S/U PAR/PDR translation | YES | Mode 2 treated as kernel |
| Split I/D (`SSR3 KD/SD/UD`) | YES | Per mode |
| MMU fault trap (`000250`) | YES | Instruction aborted |
| Vector fetch via Kernel D-space | YES | D-space when KD split enabled, else I-space |
| `SSR0/SSR2` fault state | YES | First-fault latch behavior |
| `SSR1` restart log | PARTIAL | Stores fault VA only |
| 22-bit physical memory backing | YES | Core hwstub provides 4MB physical RAM |
| `SSR3.BME` UB-map effect | NO | Bit is stored/readable; no Unibus map translation path |

---

## 19. Debugging Playbook (including SIMH)

This section collects practical debugging methods used in this repository.

### 19.1 Fast local checks

Core-only:
```sh
make test
make test-mmu-on
```

LSI11 machine profiles:
```sh
make -C lsi11 test
make -C lsi11 test-pdp1184
```

### 19.2 Emulator trace options

For `lsi11/lsi11` and `lsi11/pdp1184`:
- `-trace` instruction trace with disassembly
- `-trace-regs` register dump per instruction
- `-traceirq` interrupt delivery trace
- `-tracenxm` NXM/bus trap trace
- `-steps N` execute exactly N instructions and exit
- `-check-config` final machine/device config print

Example:
```sh
./lsi11/pdp1184 -rk lsi11/disks/SYS.DSK -bootrt11 -trace -traceirq -tracenxm > /tmp/pdp1184.trace
```

### 19.3 LLDB debugging examples

```sh
lldb -- ./lsi11/pdp1184 -rk lsi11/disks/SYS.DSK -bootrt11
```

Useful LLDB commands:
```text
(lldb) b core_step
(lldb) b nxm_trap
(lldb) run
(lldb) bt
(lldb) frame variable *r
(lldb) register read
```

For test binaries:
```sh
lldb -- tests/core_tests
lldb -- tests/test_mmu_basic
```

### 19.4 SIMH reference runs (cross-check)

Use SIMH as reference behavior when validating boot loaders, CPU ID paths,
interrupt flow, and controller polling loops.

PDP-11/04 + RK (RT-11 style):
```text
set cpu 11/04
set cpu 64k
set cpu idle
set console 7b
set tti 7b
set tto 7b
set rk0 enabled
attach rk0 lsi11/disks/SYS.DSK
boot rk0
```

PDP-11/04 + RL:
```text
set cpu 11/04
set cpu idle
set rl enabled
attach rl0 lsi11/disks/newsys.rl02
boot rl0
```

PDP-11/84 + RL:
```text
set cpu 11/84
set rl enabled
attach rl0 lsi11/disks/newsys.rl02
boot rl0
```

PDP-11/34 + HK/RK07 image:
```text
set cpu 11/34
set cpu 4M
set TTI 8B
set TTO 8B
set hk0 rk07
attach hk0 lsi11/disks/rt11v503.dsk
boot hk0
```

### 19.5 SIMH interactive debug commands

After boot:
```text
break 000604
go
step
step 20
examine 177450
examine 177440
```

### 19.6 Side-by-side comparison workflow

1. Run emulator with `-trace` and save log.
2. Run same image/config in SIMH.
3. Compare traces around first divergence (PC, PSW, CSR values, IRQ vectors).
4. If divergence starts after I/O access, add `-traceirq`/`-tracenxm`.
5. If divergence starts in CPU flow, reproduce with `tests/core_*` or MMU tests.

---

## Appendix A: Checklist Mapping

This report corresponds to:
`DCJ11_COMPLIANCE.yaml`
