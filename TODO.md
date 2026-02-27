# J-11 / LSI-11 TODO (NEW TABLES)

## Workflow

- [x] Table 1: `Instructions or Instruction sets` processed
- [x] Table 2: `Illegal Instruction Actions / JMP, JRS` processed
- [x] Table 3: `MOV/CMP/BIT/BIC/BIS/ADD/SUB, SWAB, EIS differences` processed
- [x] Table 4: `FIS differences, MFPT differences, FPP differences` processed
- [x] Table 5: `RESET instruction, MTPS/MFPS` processed
- [x] Table 6: `Memory Management Expansion and Relocation` processed
- [x] Table 7: `MMU Registers / PARs` processed
- [x] Table 8: `Interrupts` processed
- [x] Table 9: `Buses` processed
- [x] Table 10: `Processor Status Word` processed
- [x] Table 11: `Processor Status Word (T-bit and access)` processed
- [x] Table 12: `General Purpose Registers` processed
- [x] Table 13: `Error Handling (odd/bus errors)` processed
- [x] Table 14: `Error Handling (SP/stack/vector/front-panel)` processed

## Findings

Table 1 scope used for comparison:
- `VM1/VM1G/VM2` vs `PDP-11/03` base set
- `DCJ11` vs `PDP-11/84`

Table 1 quick matrix (implemented in current code):
- `BASIC`: VM1/VM1G/VM2/DCJ11 = `Y` (ok)
- `SOB,SXT`: VM1/VM1G/VM2/DCJ11 = `Y` (ok)
- `RTT`: VM1/VM1G/VM2/DCJ11 = `Y` (ok)
- `MARK`: VM1/VM1G/VM2/DCJ11 = `Y` (ok)
- `XOR`: VM1/VM1G/VM2/DCJ11 = `Y` (ok)
- `ASH,ASHC,MUL,DIV`: VM1=`N`, VM1G=`Y`, VM2=`Y`, DCJ11=`Y`
  - For `VM1`: matches 11/03 base (`N` without options)
  - For `VM1G`: extension beyond strict 11/03 base (intentional for now)
  - For `VM2`: extension beyond strict 11/03 base (allowed by current K1801 policy)
  - For `DCJ11`: `Y` (ok)
- `46 floating-point instructions (FP11 class)`: VM1/VM1G/VM2 default = `N`, DCJ11 default = `Y`
  - 11/03 base: `N` (ok)
  - 11/84 column: `Y` (ok)
- `MFPT`: VM1/VM1G=`N`, VM2=`N`, DCJ11=`Y`
  - 11/03: `N` (ok)
  - 11/84: `Y` (ok)
  - VM2 policy: follow K1801VM2 docs (`N`) within 11/03-base compatibility bucket
- `MTPS`: VM1/VM1G/VM2/DCJ11 = `Y` (ok)
- `CIS`: VM1/VM1G/VM2/DCJ11 = `N` (ok for our baseline)
- `MFPI,MFPD,MTPI,MTPD`: VM1/VM1G=`N`, VM2/DCJ11=`Y` (ok)
- `SPL`: VM1/VM1G/VM2=`N`, DCJ11=`Y` (ok)
- `CSM`: VM1/VM1G/VM2=`N`, DCJ11=`Y*` (implemented, enabled by `MMR3<3>`)

Open mismatches from Table 1:
- [ ] Decide policy for `VM1G` EIS (`MUL/DIV/ASH/ASHC`) vs strict `11/03` base: keep as extension or gate by compatibility mode.

Table 2 (`Illegal Instruction Actions`, `JMP/JRS`) summary:
- `JMP %n` (mode 0):
  - current code: `DCJ11 -> T4`, `VM1/VM1G/VM2 -> T4`
  - table columns: `11/84 -> T4`, `11/03 -> T10`
  - status: `DCJ11` match, `VM*` intentionally follows K1801 docs (not strict 11/03 table here)
- `HALT` in non-kernel mode:
  - current code: `DCJ11 -> T4` (match `11/84`), `VM*` use model-specific HALT flow
  - note: `VM*` supervisor/user split from generic PDP-11 table is not strictly applicable
- `HALT` in kernel mode:
  - current code: `VM* -> HALT path`, `DCJ11 -> HALT path`
  - table for `11/84` says `Halt or T10`; currently only `Halt` branch is modeled
- `075040..075377`, `075400..075777`:
  - current code: reserved/illegal trap (`T10`) unless actual FIS opcode subset
  - status: matches `11/84` expectation
- `076600`:
  - current code: reserved/illegal trap (`T10`)
  - status: matches `11/84` expectation
- `170000..177777`:
  - current code: `FPP` decode only if `has_fpu=1`, else `T10`
  - status: `DCJ11` default now aligned with `11/84` (`has_fpu=1`)
- `0210..0227`:
  - current code: reserved/illegal trap (`T10`)
  - table `11/03`: maintenance instruction class
  - status: currently treated as out-of-scope (non-basic set)
- `JMP/JSR mode 2` (`(R)+` destination):
  - current code/tests: `PC <- R` (original pre-increment value) for all models
  - status: matches table row (`R` for `11/03` and `11/84`)

Table 3 (`MOV/CMP/BIT/BIC/BIS/ADD/SUB`, `SWAB`, `EIS differences`) summary:
- `Same register source + auto inc/dec(/deferred) destination`:
  - current code: `DCJ11 -> R+2`, `VM1/VM1G/VM2 -> R`
  - status: matches selected baseline (`VM* as 11/03`, `DCJ11 as 11/84`)
- `PC as source + indexed/indexed-deferred destination`:
  - current code: `DCJ11 -> PC+2 semantic row` (effective `OPR+4`), `VM* -> PC semantic row` (effective `OPR+2`)
  - status: matches selected baseline
- `SWAB V-bit action`:
  - current code: `V=0` for all models
  - status: matches table
- `EIS exists`:
  - current code: `VM1=N`, `VM1G=Y`, `VM2=Y`, `DCJ11=Y`
  - status: `VM1` and `DCJ11` align; `VM1G/VM2` are extension behavior beyond strict 11/03 base
- `EIS odd-register CC basis (16 vs 32-bit)`:
  - current implementation behavior is `32-bit` basis for EIS arithmetic paths
  - `DCJ11 (11/84)` expected `32` in table -> aligned
  - test gap: no dedicated targeted unit test for this row yet

Open follow-up from Table 3:
- [ ] Add explicit unit test for EIS odd-register condition-code basis (`32-bit`), at least for `DCJ11` and `VM2`.

Table 4 (`FIS`, `MFPT`, `FPP`) summary:
- Scope still used:
  - `VM1/VM1G/VM2` vs `PDP-11/03` base
  - `DCJ11` vs `PDP-11/84`
- `FIS exists`:
  - current code defaults: `VM1/VM1G/VM2 = N`, `DCJ11 = N`
  - table baseline: `11/03 = N` (without KEV11), `11/84 = N`
  - status: match for selected baseline
- `FMUL/FDIV require one word of R6 stack`:
  - current FIS path does not push/pop via `R6`; it operates via operand pointer register
  - status: KEV11-specific stack behavior is not modeled
- `FIS is interruptible` and `CC after interrupted FIS are indeterminate`:
  - current FIS path executes atomically in one step; no explicit mid-instruction interrupt/abort model
  - status: interruptible-FIS semantics are not modeled
- `MFPT exists` / `MFPT result`:
  - current code: `VM1/VM1G/VM2 = N`, `DCJ11 = Y`, result `R0=5`
  - status: matches selected baseline (`11/03: N`, `11/84: Y with result 5`)
- `FPP microcode/hardware`:
  - current code: `DCJ11 has_fpu=1` and FP11 decode is enabled
  - status: instruction availability for `11/84` is aligned
  - timing model note: explicit async hardware-completion model is not implemented (execution is synchronous in CPU step)

Open follow-up from Table 4:
- [x] Strict `11/84` policy applied: `DCJ11` default `has_fis=0` (optional enable remains available via CLI `-force-fis`).
- [ ] If KEV11-style FIS is needed, implement missing semantics (`FMUL/FDIV` one-word `R6` stack usage + interruptible behavior/CC aftermath).

Table 5 (`RESET`, `MTPS/MFPS`) summary:
- Scope still used:
  - `VM1/VM1G/VM2` vs `PDP-11/03` base
  - `DCJ11` vs `PDP-11/84`
- `RESET instruction` timing/power-fail rows:
  - current emulator does not model real-time BUS INIT pulse durations or power-fail timing interactions
  - status: functional reset behavior is modeled, cycle/time-level reset behavior is out of scope
- `RESET` functional behavior:
  - `DCJ11`: user-mode `RESET` is NOP; kernel `RESET` clears `PIRQ`, preserves `CPUERR`, clears `MMR0/MMR3` (with MMU build), preserves `MMR1/MMR2`
  - status: aligned with intended J-11-style functional semantics used in current tests
- `MTPS/MFPS exists`:
  - current code: implemented for `VM1/VM1G/VM2/DCJ11`
  - status: aligns with selected baseline columns (`11/03 = Y`, `11/84 = Y`)
- `While in user/supervisor mode, MTPS changes PSW<3:0>`:
  - `DCJ11`: non-kernel `MTPS` updates low condition bits while preserving priority (`<7:5>`)
  - status: aligns for `11/84`
- `While in user/supervisor mode, MTPS changes PSW<7:4>`:
  - `DCJ11`: non-kernel `MTPS` preserves `<7:5>` and `T` -> `N` behavior
  - `VM*`: current implementation can change priority-related low-byte bits via `MTPS` (while still preserving `H/T`)
  - status: `DCJ11` aligned; `VM*` is K1801-specific and not strict `11/03` interpretation
- `While in user/supervisor mode, MFPS accesses PSW<7:0>`:
  - current code reads low PSW byte for all models
  - status: aligns with selected baseline expectation

Open follow-up from Table 5:
- [x] Keep current K1801-specific `MTPS/MFPS` behavior for `VM1/VM1G/VM2` (do not enforce strict `11/03` non-kernel `MTPS` restriction).

Table 6 (`Memory Management Expansion and Relocation`) summary:
- Scope still used:
  - `VM1/VM1G/VM2` vs `PDP-11/03` baseline
  - `DCJ11` vs `PDP-11/84`
- `Number of physical address bits` / `Maximum physical memory`:
  - `VM*` path is 16-bit/fixed-56KB style machine profile (`lsi11` bus model)
  - `DCJ11` path supports 22-bit physical addressing (`MMR3<5>` + 22-bit bus model), default machine RAM 4096KB with 8KB I/O page hole (`4088KB` usable RAM space)
  - status: aligned with selected baseline capability model
- `Kernel/Supervisor/User mode`:
  - `VM*`: no full MMU mode set (03-style baseline)
  - `DCJ11`: all K/S/U modes implemented, with banked stacks and dual register set logic
  - status: aligned with baseline intent
- `Instruction space` / `Separate data space`:
  - `VM*`: no split I/D
  - `DCJ11`: I-space always, D-space split controlled per mode by `MMR3<KD,SD,UD>`
  - status: aligned
- `Action on any access with PSW<15:14>=10 (reserved current mode)`:
  - `DCJ11` MMU path: explicit MMU abort to vector `0250` (covered by test `dcj11_mmu_illegal_mode2_aborts`)
  - status: aligned with table row (`T250`)
- `Which SP do MFPI/MFPD/MTPI/MTPD use when PSW<13:12>=10`:
  - table `11/84`: `User`
  - current `DCJ11` code: previous mode is normalized via helper (`mode 2 -> kernel`), so stack-bank selection is effectively kernel-side for that case
  - status: mismatch against table expectation
- `PSW<15:12>, multiple SPs, and MxPI ops with/without MMU option`:
  - `DCJ11`: implemented in core path independently of MMU-on translation flow
  - status: aligned for practical behavior

Open follow-up from Table 6:
- [ ] Decide/implement `DCJ11` behavior for `MFPI/MFPD/MTPI/MTPD` when `PSW<13:12>=10`: keep current mode2->kernel normalization or switch to table-expected user SP behavior.
- [ ] Add targeted test for `PSW<13:12>=10` stack-bank selection in `MFPI/MFPD/MTPI/MTPD` after policy is fixed.

Table 7 (`MMU Registers`, `PARs`) summary:
- Scope still used:
  - `VM1/VM1G/VM2` vs `PDP-11/03` baseline
  - `DCJ11` vs `PDP-11/84`
- `MMR0 <08> maintenance`:
  - `DCJ11` mask matches J-11/SIMH style (`MM0_J=0160177`), bit 8 not implemented
  - status: aligned (`11/84 = N`)
- `MMR0 <12> MMU trap flag`:
  - not implemented in current J-11 mask/path
  - status: aligned (`11/84 = N`)
- `MMR1`:
  - implemented and updated during effective-address stack/register deltas
  - status: aligned (`11/84 = operates`, not `always 000000`)
- `MMR2`:
  - tracks instruction fetch start PC (`mmu_mmr1_instruction_start`)
  - does not track interrupt vectors as separate feed
  - status: aligned (`11/84`: fetch `Y`, vectors `N`)
- `MMR3 bits <00:02>` (D-space enables):
  - implemented and used per mode (`KD/SD/UD`)
  - status: aligned (`11/84 = Y`)
- `MMR3 bit <03>`:
  - implemented as `CSM` enable gate
  - status: aligned (`11/84 = Y`)
- `MMR3 bit <04>` (22-bit enable):
  - implemented (`M22E`) and used in physical address finalize logic
  - status: aligned (`11/84 = Y`)
- `MMR3 bit <05>` (UB map enable):
  - bit is writable/readable via MMR3 mask, but no actual Unibus map functionality in current emulator (`lsi11/README.md`: no Unibus map)
  - status: mismatch vs full `11/84` expectation
- `PAR width`:
  - `DCJ11` uses 16-bit PAR mask (`0177777`)
  - status: aligned (`11/84 = 16`)
- `Program can execute from PAR`:
  - current code path allows instruction fetch from MMU register addresses (`mmu_io_read_word` does not block ifetch)
  - table `11/84` row is `N`
  - status: mismatch
- `PAR bit <00>` trap-any-access, bit <07> any-access-flag:
  - no dedicated PAR semantics implemented
  - status: aligned for `11/84 = N`
- `PAR bit <15>` bypass cache:
  - cache subsystem is not modeled; bypass-cache effect is not modeled as hardware behavior
  - status: partial/stub semantics only

Open follow-up from Table 7:
- [ ] Implement or explicitly document `MMR3<5>` (UB map enable) as unsupported for `DCJ11` profile.
- [ ] Decide policy for instruction fetch from MMU register block (`PAR/PDR/MMR*`) on `DCJ11`: enforce non-executable internal-reg behavior (`11/84` table says `N`) or keep current permissive behavior.
- [ ] If non-executable policy is chosen, add targeted test for ifetch from `0177572..0177660` range (expect vector 4 / bus error path per chosen model policy).

Table 8 (`Interrupts`) summary:
- Scope still used:
  - `VM1/VM1G/VM2` vs `PDP-11/03` baseline
  - `DCJ11` vs `PDP-11/84`
- `Number of interrupt (BR) levels`:
  - `DCJ11`: 4-level priority model is implemented
  - `VM*`: current K1801 IRQ path uses model-specific masking plus priority selection in dispatcher (not strict single-level `11/03`)
  - status: `DCJ11` aligned; `VM*` intentionally K1801-specific
- `Expected interrupt occurs if PSW<7:5> lowered for only one instruction`:
  - IRQ polling is performed after each instruction, so pending IRQ is accepted in that one-instruction window
  - status: aligned for practical behavior
- `Can ISR itself be interrupted before executing its first instruction`:
  - current core checks IRQ only after instruction execution; after vector entry, first handler instruction runs before next IRQ arbitration
  - table for `11/84` indicates `Y`
  - status: mismatch
- `EIS`/`FPP` interruptibility rows:
  - current implementation executes instruction atomically per `core_step` (no mid-instruction device-interrupt abort)
  - status: aligns with `11/84` rows that expect non-interruptible behavior (`N`)
- `FIS` row:
  - `DCJ11` default `FIS` is disabled; VM2 FIS path (when enabled/trapped) is still single-step, not asynchronously interruptible
  - status: no additional divergence beyond already tracked FIS policy/semantics
- `CIS (DIS)` block (`Exists`, `Is cleared by reset`):
  - current emulator has no implemented CIS option path
  - if interpreted as CIS capability row for `11/84` (`Y`), this is a mismatch
  - reset-clearing semantics for CIS are therefore not modeled

Open follow-up from Table 8:
- [ ] Decide whether to implement pre-first-instruction ISR preemption behavior (`11/84` table `Y`) or keep current post-instruction-only IRQ arbitration model.
- [ ] If ISR preemption behavior is required, add targeted regression test: higher-priority IRQ pending at vector-entry must preempt before first handler opcode.
- [ ] Decide policy for `CIS` capability on `DCJ11/11-84` profile: keep unsupported or add optional CIS presence/reset semantics.

Table 9 (`Buses`) summary:
- Scope still used:
  - `VM1/VM1G/VM2` vs `PDP-11/03` baseline
  - `DCJ11` vs `PDP-11/84`
- `Buses available (Q-bus/UNIBUS)`:
  - current emulator uses a unified synthetic bus layer (`lsi11/bus.c`) and does not expose strict electrical Q-bus vs UNIBUS behavior per CPU model.
  - practical profile mapping is machine-level: `lsi11` target is fixed 16-bit I/O-page model, `pdp1184` target adds 18/22-bit I/O alias decode.
  - status: functional profile split exists, but strict table-level bus identity is only partial.
- `Special memory bus` (`PMI` for `11/84`):
  - no explicit PMI/fast-memory side bus model exists.
  - status: not implemented.
- `Bus cycles utilized` rows (`CLR/SXT`, `MOV`, `EIS` DATI/DATIP/DATO details):
  - bus-cycle micro-sequences are not modeled as explicit DATI/DATIP/DATO transactions.
  - current core models architectural effects and memory/device access results, not per-cycle protocol.
  - status: cycle-accurate row behavior is out of scope in current implementation.
- `UNIBUS/Q-bus timeout value`:
  - no microsecond timeout model (`10us`, `15us`, etc.) is implemented; NXM/timeout is handled as immediate access outcome.
  - status: timing-value rows are not implemented.
- `NPRs (DMA) granted during CPU instructions`:
  - DMA is polled/service-driven from device polls (`lsi11_poll_devices`), not bus-cycle arbitration during an instruction.
  - status: functional DMA exists, arbitration-timing semantics are simplified.
- `Console SLU accessible from bus` and `Line clock register accessible from bus`:
  - DL11 (`0177560..0177567`) and KW11 (`0177546..`) are bus-visible in current emulator on both machine profiles.
  - if `11/84` table interpretation requires these to be not bus-accessible, current implementation diverges.
- `Bootstrap ROMs accessible from bus`:
  - no dedicated bus-mapped bootstrap ROM device model; boot paths use helper loaders (`-bootcopy`, `-bootrt11`) and an injected RL bootstrap routine.
  - status: ROM visibility semantics differ from hardware table.

Open follow-up from Table 9:
- [ ] Decide whether to keep current unified bus model or introduce explicit per-profile bus identity contract (`Q-bus` vs `UNIBUS`) in docs/code.
- [ ] Decide policy for `DCJ11/11-84` console/line-clock bus visibility rows (keep DL11/KW11 bus-visible for software compatibility vs enforce table behavior).
- [ ] If bus-cycle accuracy is required, define minimal DATI/DATIP/DATO tracing/model boundary (at least for `MOV`, `CLR/SXT`, `EIS source fetch`).
- [ ] Decide whether bootstrap ROM behavior should stay loader-based or be emulated as a bus-visible ROM device.

Table 10 (`Processor Status Word`) summary:
- Scope still used:
  - `VM1/VM1G/VM2` vs `PDP-11/03` baseline
  - `DCJ11` vs `PDP-11/84`
- `PSW bits <15:12> mechanized (current/previous mode)`:
  - `DCJ11`: implemented (`dcj11_psw_*mode`, trap path updates, stack-bank switching) -> aligned with `11/84 = Y`.
  - `VM*`: no native PDP mode field semantics (K1801-specific PSW handling) -> baseline-aligned with `11/03 = N`.
- `PSW bit <11> mechanized (register set selection)`:
  - `DCJ11`: implemented (`rset_bank[2][6]`, `dcj11_switch_regset`) -> aligned with `11/84 = Y`.
  - `VM*`: no dual register-set switching by PSW<11> -> baseline-aligned with `11/03 = N`.
- `PSW bit <08> mechanized (CIS instruction suspended)`:
  - `DCJ11`: CIS option/bit-8 suspend semantics are not implemented.
  - current code still uses bit 8 symbolically as generic `FLAG_H` in shared paths, but not as CIS-suspend state machine.
  - status: mismatch vs `11/84` table (`Y` expected for CIS-suspend mechanization).
  - `VM*`: bit 8 exists with K1801 HALT/USER meaning, not CIS meaning (outside strict row semantics).
- `PSW bit <07> mechanized (high-order priority bit)`:
  - `DCJ11`: priority mask logic uses `PSW<7:5>` in IRQ accept/poll -> aligned.
  - `VM*`: priority/mask behavior uses K1801-specific PSW bits including bit 7 (`FLAG_P`) -> practical alignment for baseline row `Y`.

Open follow-up from Table 10:
- [ ] Decide policy for `DCJ11` PSW bit `<8>`: keep current non-CIS usage/stub behavior or implement proper CIS-suspend semantics for `11/84` compatibility.
- [ ] If CIS bit `<8>` is not implemented, document this explicitly in `README`/compat matrix to avoid ambiguity with shared `FLAG_H` naming.

Table 11 (`Processor Status Word` continuation) summary:
- Scope still used:
  - `VM1/VM1G/VM2` vs `PDP-11/03` baseline
  - `DCJ11` vs `PDP-11/84`
- `Bits <06:05> mechanized (low-order priority bits)`:
  - `DCJ11`: implemented via `PSW<7:5>` mask logic in IRQ acceptance and `SPL` handling -> aligned with `11/84 = Y`.
  - `VM*`: standard PDP `PSW<6:5>` priority model is not used as primary mechanism (K1801-specific gating with `FLAG_P`/model logic) -> baseline-compatible with `11/03 = N`.
- `Bit <04> mechanized (T)`:
  - implemented in core trace path (`FLAG_T`, trace trap vector `014`) for all models.
  - status: aligned with table row `Y`.
- `Bits <03:00> mechanized (N,Z,V,C)`:
  - condition codes are fully modeled in ALU/instruction paths.
  - status: aligned.
- `PSW accessible via reads/writes at 177776`:
  - current code allows direct access for all models (`offset == 0177776` read/write path).
  - `DCJ11/11-84` expects `Y`: aligned.
  - `VM*` in `11/03` bucket expects `N`: mismatch if strict 11/03 behavior is required.
- `MTPS/MFPS instructions`:
  - implemented for all models.
  - `DCJ11/11-84` and `11/03` baseline both expect `Y`: aligned.
- `SPL instruction`:
  - implemented only for `DCJ11`; non-DCJ11 traps as illegal.
  - status: aligned with selected baseline (`11/03 = N`, `11/84 = Y`).
- `Condition code instructions`:
  - implemented via `000240..000277` decode path (SET/CLR condition code bits).
  - status: aligned.
- `T-bit differences`:
  - `Can explicit PSW reference set/clear T-bit`:
    - `DCJ11`: explicit PSW references now preserve old `T` (cannot set/clear via `177776` writes).
    - status: aligned with `11/84` table row `N`.
  - `Instructions between RTI setting T and TRACE trap`:
    - `DCJ11`: TRACE now occurs with zero intervening instructions (`0`).
    - status: aligned with `11/84`.
  - `Instructions between RTT setting T and TRACE trap`:
    - `DCJ11`: TRACE now occurs after one intervening instruction (`1`).
    - status: aligned with `11/84`.
  - `T-bit trap immediately ends WAIT`:
    - `DCJ11`: WAIT now exits on pending T-bit TRACE condition (TRACE vector taken before IRQ polling).
    - status: aligned with `11/84` row `Y`.
  - `T-bit traps prioritized over interrupts`:
    - when trace is pending (`do_trace`), IRQ polling is skipped and TRACE vector is taken first.
    - status: aligned with `11/84 = Y`.

Open follow-up from Table 11:
- [ ] Decide policy for strict `11/03` compatibility on PSW address `177776` for `VM*`: keep K1801-permissive direct PSW access or block it.
- [x] For `DCJ11`, enforce `11/84` T-bit timing:
  - `RTI`: TRACE with zero intervening instructions.
  - `RTT`: TRACE with one intervening instruction.
- [x] For `DCJ11`, implement `WAIT` termination by pending T-bit TRACE condition (not IRQ-only wakeup).
- [x] Decide whether direct PSW write should be prevented from changing `T` on `DCJ11` per table row, or keep current behavior and document deviation.
  - policy chosen: prevent explicit PSW references from changing `T` on `DCJ11` (`11/84` behavior).

Table 12 (`General Purpose Registers`) summary:
- Scope still used:
  - `VM1/VM1G/VM2` vs `PDP-11/03` baseline
  - `DCJ11` vs `PDP-11/84`
- `Number of sets of R0-R5`:
  - `DCJ11`: dual bank for `R0..R5` implemented (`rset_bank[2][6]`, switched by `PSW<11>`) -> aligned with `11/84 = 2`.
  - `VM*`: single register set -> aligned with `11/03 = 1`.
- `Number of stack pointers`:
  - `DCJ11`: banked SP by mode via `sp_mode[4]` with mode `2` normalized to kernel, i.e. effectively 3 useful SPs -> aligned with `11/84 = 3`.
  - `VM*`: one architectural SP in current core model -> aligned with `11/03 = 1`.
- `Can program code be executed from GPRs?`:
  - `DCJ11`: ifetch from modeled CPU internal register block is explicitly aborted (`core_load_word_ex` check on ifetch) -> aligned with `11/84 = N`.
  - `VM*`: no explicit GPR memory-map is implemented, so strict “execute from GPRs” is `N` by model intent.
- `Can GPRs be accessed by program as 1777700-1777717?`:
  - current code does not map that range to real `R0..R7` for any model.
  - `DCJ11/11-84`: aligns with `N`.
  - `VM*`: aligns with `11/03 = N` for GPR meaning, but with caveat below.
- `Can GPRs be accessed by console as 1777700-1777717?`:
  - console ODT-level direct CPU-register addressing is not modeled as a separate path.
  - status: effectively `N`/not implemented for our profiles, `DCJ11` aligned with table `N`.
- Implementation caveat (important):
  - for `VM1/VM1G`, addresses `0177700..0177712` are currently decoded as a VM1 internal register block (`vm1_reg_block_*`), and ifetch from this block is not blocked.
  - this is not GPR mapping, but it means code fetch from that internal-reg area can occur in current implementation.

Open follow-up from Table 12:
- [ ] Decide policy for `VM1/VM1G` ifetch from internal register block `0177700..0177712`: keep permissive behavior or block instruction fetch for stricter `11/03`-style semantics.
- [ ] Document explicitly that `0177700..0177717` is not mapped to architectural GPRs in current emulator (to avoid confusion with the family table row wording).

Table 13 (`Error Handling`) summary:
- Scope still used:
  - `VM1/VM1G/VM2` vs `PDP-11/03` baseline
  - `DCJ11` vs `PDP-11/84`
- `Odd address errors detected by CPU`:
  - `DCJ11`: word access odd-address checks are implemented in core word paths (`core_load/store_word*`) with bus-error trap -> aligned with `11/84 = Y`.
  - `VM*`: no generic odd-word-address trap path in current core/bus callbacks -> aligned with `11/03 = N`.
- `If odd address error occurs while autoinc/autodec register mode is used, register will have been modified`:
  - `DCJ11`: addressing decode updates register before memory access (`decode_data` modes 2/3/4/5), no rollback on trap -> aligned with `11/84 = Y`.
  - `VM*`: row is effectively N/A for strict `11/03` because odd-address trap itself is not generated.
- `Bus error (timeouts/NXM) detected by CPU`:
  - `DCJ11`: NXM/timeout path sets CPUERR bits and vectors via bus-error trap -> aligned with `11/84 = Y`.
  - `VM*`: NXM detected in adapter bus callbacks and trapped to vector 4 path -> aligned with `11/03 = Y`.
- `If bus error occurs with autoinc/autodec addressing, register will have been modified`:
  - both `DCJ11` and `VM*`: register side-effect is applied in decode before memory access; fault does not revert register -> aligned for `11/84 = Y` and `11/03 = Y`.
- `If bus error occurs while reading I-stream using PC, PC will have been incremented`:
  - policy fixed: interpret this row as *instruction fetch* (`Read I-stream via PC for opcode word`), matching table intent.
  - `VM*` (`11/03` bucket): on instruction-fetch bus error, stacked `PC` is now incremented (`PC+2`) -> aligned with `Y`.
  - `DCJ11` (`11/84` bucket): on instruction-fetch bus error, stacked `PC` remains non-incremented -> aligned with `N`.
  - targeted regression tests added for `VM1/VM1G/VM2/K1806VM2` and `DCJ11`.

Open follow-up from Table 13:
- [x] Finalize interpretation of row `bus error while reading I-stream using PC` (pure instruction fetch only vs any PC-driven I-stream read including extension words).
- [x] After policy is fixed, implement per-model `PC` update on I-stream bus error (`VM*` as `11/03`, `DCJ11` as `11/84`) and add targeted regression tests.
- [x] Reconcile Table 13 `PC` bus-error row with previously tracked old item 28 semantics (avoid duplicate/conflicting policy notes).

Table 14 (`Error Handling` continuation) summary:
- Scope still used:
  - `VM1/VM1G/VM2` vs `PDP-11/03` baseline
  - `DCJ11` vs `PDP-11/84`
- `Errors using (SP): odd address / bus error while using (SP)`:
  - `DCJ11`: red-stack fallback path exists (`dcj11_take_red_stack_abort`) with emergency stack at `SP=000004` and vector `000004` -> broadly aligned with `11/84` style `SP<-4, T4`.
  - `VM*`: per `KM1801VM2` / `К1801ВМ1` docs, timeout/error processing remains interrupt/vector based (`004`, stack save), with `SP` side effects preserved; no explicit `(SP)->HALT` rule found.
  - status: keep current vector-4 path for `VM*` as K1801-specific behavior; strict `11/03` `HALT` row is not enforced.
- `Stack overflow errors / yellow-zone trap implemented`:
  - `DCJ11`: implemented (`dcj11_note_stack_reference`, pending yellow trap serviced after instruction) -> aligned with `11/84 = Y`.
  - `VM*`: not implemented -> aligned with `11/03 = N`.
- `Yellow-zone trap programmable (else fixed at 000400)`:
  - `DCJ11`: fixed threshold constant `DCJ11_STACK_YEL_LIMIT=0000400`, not programmable.
  - status: aligned for `11/84` column where programmable flag is `N`.
- `Yellow-zone action`:
  - `DCJ11`: execute instruction then take trap (`dcj11_yellow_pending` serviced at `step_end`) -> aligned with `Execute then T4`.
- `Separate red-zone trap implemented / red-zone action`:
  - `DCJ11`: no separate threshold-based red-zone detector; only red fallback on abort during vector push.
  - status: consistent with `11/84` column (`N` for separate red-zone trap), but behavior is partial/exception-path only.
- `Vector errors: error while fetching error vector hangs processor`:
  - current emulator does not model a dedicated hardware “hang” latch/state; vector-fetch faults go through normal abort/trap flow.
  - status: simplified, not cycle/hardware-accurate to this row.
- `Halt switch processor / console initialize able to halt / console RESTART able to restart`:
  - core has internal `fHaltSignal` path, but full front-panel switch model (HALT/INIT/RESTART hardware semantics) is not fully emulated in user-facing machine profiles.
  - status: table rows are only partially modeled.

Open follow-up from Table 14:
- [x] Decide `VM*` policy for `error using (SP)` row: keep current vector-4 path (K1801 docs-driven), do not enforce strict `11/03` `HALT`.
- [ ] Define whether to model explicit “vector-fetch error hangs CPU” state or keep current recoverable abort/trap behavior.
- [ ] Decide scope for front-panel semantics (`HALT switch`, `INIT halting`, `RESTART`) and either implement or explicitly document as out-of-scope.

## Prioritized Backlog

### P0 (fix first)
- [x] `DCJ11` T-bit conformance (`Table 11`):
  - enforce `RTI` trace timing (`0` intervening instructions),
  - enforce `RTT` trace timing (`1` intervening instruction),
  - make `WAIT` exit on pending T-bit trace condition,
  - finalize policy for direct PSW write changing `T` on `11/84`.
- [x] I-stream bus-error `PC` semantics (`Table 13`):
  - finalize row interpretation,
  - implement per-model (`VM*` as `11/03`, `DCJ11` as `11/84`),
  - add regression tests and reconcile with old item 28 notes.
- [x] `VM*` error-using-`SP` behavior (`Table 14`):
  - policy fixed to K1801 docs behavior: keep vector-4 path, add regression tests.
- [ ] `DCJ11` `MFPI/MFPD/MTPI/MTPD` with `PSW<13:12>=10` (`Table 6`):
  - choose stack-bank behavior policy,
  - add targeted test.
- [ ] `DCJ11` ifetch from MMU internal register block (`Table 7`):
  - finalize policy (non-executable vs permissive),
  - implement and add targeted test.

### P1 (next)
- [ ] `MMR3<5>` UB-map semantics (`Table 7`): implement or document as unsupported.
- [ ] IRQ preemption before first ISR instruction (`Table 8`): decide policy and add test if enabled.
- [ ] `VM1/VM1G` ifetch from internal reg block `0177700..0177712` (`Table 12`): decide permissive vs block.
- [ ] Document `0177700..0177717` non-GPR mapping clearly (`Table 12`).
- [ ] Strict `11/03` policy for PSW address `177776` on `VM*` (`Table 11`).
- [ ] CIS policy (`Tables 8/10`):
  - decide optional CIS capability for `DCJ11`,
  - decide PSW bit `<8>` behavior and document.
- [ ] Error-vector-fetch hang semantics (`Table 14`): decide model vs simplified abort flow.
- [ ] Front-panel semantics scope (`Table 14`): HALT/INIT/RESTART behavior implement vs document out-of-scope.
- [ ] `VM1G` EIS policy vs strict `11/03` base (`Table 1`).
- [ ] Add explicit EIS odd-register CC-basis test (`Table 3`).

### P2 (optional / accuracy extensions)
- [ ] KEV11-style FIS semantics (`Table 4`): `FMUL/FDIV` stack usage and interruptibility/CC aftermath.
- [ ] Bus-model fidelity (`Table 9`):
  - explicit `Q-bus` vs `UNIBUS` contract,
  - console/line-clock visibility policy for `11/84`,
  - define DATI/DATIP/DATO modeling boundary,
  - bootstrap ROM as bus-visible device vs loader helper.

## Open Questions

- If generic PDP-11 matrix conflicts with K1801 documentation, apply K1801 docs for VM1/VM1G/VM2 first.
- Keep `VM1/VM1G/VM2` `JMP %n -> T4` as K1801-specific behavior (despite `11/03` table showing `T10`)?
- Are `0210..0227` maintenance opcodes required in our scope, or remain intentionally unimplemented as non-basic?
