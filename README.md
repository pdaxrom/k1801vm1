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

Mode bits: **LIMITED**
- No user/kernel memory spaces or MMU; PSW mode bits do not affect memory access.

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

References: J‑11 Programmer’s Reference (instruction set).

---

## 8. DCJ11‑Specific Features (SEL Registers / Signals)

### 8.1 Identification
SEL‑related entities implemented in `core/`:
- SEL0 — unaddressed SEL register (CPU‑internal)
- SEL1 — external register at 0177716
- SEL2 — external register at 0177714

### 8.2 Semantics
Implemented behavior:
- Reset PC uses SEL0 high byte: PC ← (SEL0 & 0177400), PSW ← 000340
- RSEL reads SEL0 into R0 (for VM2/DCJ11)
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
- DCJ11 supports VM2 privileged instructions (GO/STEP/RSEL/RCPC/RCPS/WCPC/WCPS)
- DCJ11 interrupt priority masking behavior follows J‑11 rules

Impact:
- VM1 software expecting SEL behavior still works at the CPU level; device‑level
  SEL signaling is not emulated.

References: `doc/1801vm.txt`, `doc/KM1801VM2.pdf`.

---

## 10. Differences vs K1801VM2 (Summary)

Key differences (within scope):
- No MMU or separate user/kernel memory space in this core
- HALT/SEL vectoring follows implemented DCJ11 rules (SEL0)

Impact:
- Supervisor/user address space distinctions are not enforced.

References: `doc/KM1801VM2.pdf`.

---

## 11. Diagnostic Tests

Implemented tests (see `tests/core_tests.c`):
- trap/interrupt stack frame order
- RTI restore order
- WAIT + interrupt resume
- IRQ priority masking and highest‑priority selection
- TSTB flags and BPL branching
- DCJ11 SEL0/SEL1/SEL2 semantics

Run:
```
make test
```

---

## 12. Known Limitations

- No MMU or separate user/kernel spaces.
- External bus timing and SEL/DIN/DOUT/RPLY signal timing are not modeled.
- Device interrupt arbitration is outside the core (handled by machine layer).

---

## 13. Conclusion

Overall DCJ11 compliance status: **MOSTLY COMPLIANT** for CPU‑level semantics.
Core behavior matches documented trap/interrupt rules and tested instruction
semantics, with explicit limitations around MMU and device‑level bus signaling.

---

## Appendix A: Checklist Mapping

This report corresponds to:
`DCJ11_COMPLIANCE.yaml`
