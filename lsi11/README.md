## LSI-11 Subproject: Split Machine Profiles

All addresses, vectors, and priorities are **octal**.

This subproject now provides two separate executables:

1. `lsi11` (PDP-11/04-like profile)
2. `pdp1184` (PDP-11/84-like profile, DCJ-11)

### Bus model policy (current scope)
- Unified synthetic bus model is used for both targets; strict electrical `Q-bus` vs
  `UNIBUS` behavior is not emulated.
- `pdp1184` adds 18/22-bit I/O-window alias decode to the same 16-bit device page.
- DATI/DATIP/DATO per-cycle timing/protocol fidelity is out of scope.
- DL11 remains bus-visible on both profiles; the default system clock is an
  onboard `LTC` with a `KW11-L`-compatible interface at `0177546`, while
  `KW11-P` is available only as an explicit compatibility override.
- Bootstrap flow is loader-based (`-boot`, `-bootcopy`, `-bootrt11`), not bus-ROM emulation.
- Full hardware front-panel semantics (`HALT switch`, `INIT`-driven halt,
  `RESTART` sequencing) are out of scope for both targets.

## 1) `lsi11` target (PDP-11/04-like, NO MMU)

### Identity and MMU policy
- Built with `ENABLE_MMU=0` (MMU not compiled in).
- This target is intentionally fixed-function and does not require MMU runtime setup.
- Built with `MMU_STUB_REGS_WHEN_DISABLED=0` so MMU CSR stubs do not overlap LP11 CSR addresses.
- RH11 is available in this profile at `0177440..0177462`.

### Memory map (fixed)
| Range (octal) | Size | Meaning |
|---|---:|---|
| `000000–157777` | 56 KB | RAM |
| `160000–177777` | 8 KB | I/O page (device CSRs only) |

### VM2 USER/HALT RAM banking
- For CPU model `k1801vm2`/`k1806vm2`, CPU RAM accesses in `000000..157777`
  use VM2 USER/HALT bank policy in machine layer.
- Default USER bank size: `0200000` bytes (64KB).
- Default HALT bank size: `0000000` bytes (HALT RAM absent, HALT RAM access -> NXM).
- Optional HALT bank size: `0200000` bytes (distinct HALT RAM bank).
- I/O page `160000..177777` is never banked and keeps existing decode/NXM behavior.

### DL11 alias requirement
- DL11 alias mapping is enabled and preserved:
  - `0176500–0176507` aliases `0177560–0177567`

### Boot/usage
- RT-11 boot with RK11 loader:
  - `./lsi11 -rk disks/rt11v400.dsk -bootrt11`
- Explicit unit boot:
  - `./lsi11 -rk disks/rt11v400.dsk -boot rk0`
  - `./lsi11 -rk disk0.dsk -rk disk1.dsk -boot rk1`
- RH11 image attach:
  - `./lsi11 -rh disks/rk07.img`
- RL image attach (auto RL01/RL02 detect):
  - `./lsi11 -rl disks/rl02.dsk`
- Multi-unit attach (repeat option to fill units in order):
  - `./lsi11 -rk disk0.dsk -rk disk1.dsk` -> `rk0`, `rk1`
  - `./lsi11 -rh disk0.dsk -rh disk1.dsk` -> `rh0`, `rh1`
  - `./lsi11 -rl disk0.dsk -rl02 disk1.dsk` -> `rl0`, `rl1`
  - limits: `rk/rh/tq` up to `8` units, `rl` up to `4` units

## 2) `pdp1184` target (PDP-11/84-like, MMU enabled)

### Identity and MMU policy
- Built with `ENABLE_MMU=1`.
- MMU registers and translation are available in CPU core.
- Default CPU model is `dcj11`.
- Optional override: `-cpu dcj11|11/03|11/84|11/34|k1801vm1|k1801vm2|k1806vm2`.
- CPU-visible processor registers are implemented in `core/` (CPU-level), not in machine device map:
  - `0177750` (DCJ11 register)
  - `0177776` (PSW)
- In `pdp1184` profile, `0177750` reset value is initialized to `001045` to match
  RT-11/SIMH PDP-11/84 CPU identification path.

### RAM size model
- Default RAM size: **4096 KB**.
- RAM size is configurable with `-ram <kb>` or `--mem-kb <kb>`.
- Host allocation/mapping page size is fixed:
  - `PAGE_SIZE = 4096` bytes (4 KB)
- RAM must be a multiple of page size:
  - `ram_kb % 4 == 0`
- Invalid sizes are rejected with a clear error.
  - Example: `4101 KB` invalid
  - Example: `4104 KB` valid

### I/O compatibility
- RL11, RK11, RH11, DL11, DZ11, KW11-L/LTC, optional KW11-P override, LP11, SR are supported.
- DL11 alias is **not forced** by default on this target.
- DL11 terminal character width defaults to **7-bit** (UNIX V5 compatible).
  - Use `-tty8b` for 8-bit console behavior.
- If needed for compatibility, alias can be explicitly enabled:
  - `-dl11-alias`
- Bus decode model:
  - low 16-bit page `160000..177777`: registered device CSRs are decoded there
  - undecoded addresses in `160000..177777` are NXM (reserved I/O page, not RAM)
  - higher 18-bit/22-bit I/O windows are decoded to the same 16-bit device page for registered devices
  - for non-device addresses outside RAM range, bus returns NXM as usual

### DZ11 terminal multiplexer
- DZ11 controller is bus-visible by default:
  - CSR base `0160100` (`0160100..0160107`)
  - vectors: RX `000300`, TX `000304`
  - priority: `5`
  - `8` serial lines per controller
- Host connectivity:
  - `-dz <tcp-port>` enables a TCP listener and maps incoming client sessions to DZ lines
  - `-disable-dz` hides DZ11 from the bus
- Character width follows console mode:
  - default 7-bit
  - `-tty8b` enables 8-bit path for DL11 and DZ11

### RH11 controller
- RH11 (Massbus adapter) is available in both `lsi11` and `pdp1184`.
- CSR map (octal), 16-bit I/O page view, base `0177440`:
  - `0177440 RHCS1`, `0177442 RHWC`, `0177444 RHBA`, `0177446 RHDA`
  - `0177450 RHCS2`, `0177452 RHDS`, `0177454 RHER`, `0177456 RHAS`
  - `0177460 RHLA`, `0177462 RHDB`
- IRQ: vector `000210`, priority `5` (RK611 default from EK-RK067-UG-001).
- IRQ semantics follow project latch rules:
  - edge-triggered and latched, no repeat while `DONE=1`
  - ACK clears only `irq_req`
  - new `GO` clears `DONE` (software clear) and re-arms IRQ edge
  - IE toggles do not generate an IRQ by themselves
- Supported commands (phase 1):
  - `READ`, `WRITE`, `SEEK` (multi-unit; unit selected by `RHCS2<2:0>`)
- DMA model:
  - `RHBA` uses low 16-bit physical bus address
  - `RHCS1` bits `BA16/BA17` extend DMA to 18-bit physical address space
  - no Unibus map (22-bit map is not implemented)
  - DMA uses bus physical accesses only, MMU translation is not used
  - NXM during DMA sets error and still completes command with `DONE`
- Disk mapping (phase 1):
  - flat image file
  - `RHDA[5:0]` sector, `RHDA[15:6]` linear track
  - `LBA = track * 64 + sector`, sector size `512` bytes (`01000` octal)

### TK50 (TMSCP/TQ) support
- `TQ11` models a minimal `TMSCP` tape controller for `TK50`-class units (`unit 0..7`).
- Default controller interface (octal):
  - CSR/port base `0174500`
  - `IP` at `0174500`
  - `SA` at `0174502`
  - vector `000260`
  - priority `5` (`BR5`)
- The controller is opt-in:
  - hidden by default (`dev_tq=0`)
  - enabled automatically when `-tq <path>` is specified
  - can be forced off with `-disable-tq`
  - repeated `-tq` attaches consecutive units (`tq0`, `tq1`, ...)
- Image backend:
  - SIMH `.tap` format
  - variable-length records
  - tape marks
  - forward/reverse spacing
  - rewind
- Implemented command subset:
  - `GET UNIT STATUS`
  - `GET COMMAND STATUS`
  - `AVAILABLE`
  - `ONLINE`
  - `SET UNIT CHARACTERISTICS`
  - `FLUSH`
  - `ACCESS`
  - `COMPARE`
  - `READ`
  - `WRITE`
  - `SPACE`
  - `REWIND`
  - `WRITE TAPE MARK`
- Attention:
  - when `CF_ATN` is enabled by `SET CONTROLLER CHARACTERISTICS`, the controller
    can post `UNIT NOW AVAILABLE` attention (`OP_AVA`) for an attached tape
- Attach example:
  - `./pdp1184 -tq tapes/tk50.tap`
  - `./lsi11 -tq tapes/tk50.tap`
  - `./pdp1184 -tq tq0.tap -tq tq1.tap` -> `tq0`, `tq1`
- Boot note:
  - `-boot tq0` boots via built-in `TQ/TMSCP` bootstrap from selected `tqN`
  - `-boottq` installs a built-in `TQ/TMSCP` bootstrap at `016000`
  - example: `./pdp1184 -tq disks/ultrix/ultrix31.tap -boottq -ram 4096`
  - this follows the `SIMH` `BOOT TQ0` style bootstrap for unit `0`
  - `-bootrt11` remains disk-only (`RK`/`RH`/`RL`)
- Tracing:
  - `LSI11_TRACE_TQ=1 ./pdp1184 -tq tapes/tk50.tap ...`
  - logs controller init, packet flow, and IRQ delivery in octal

### Examples
- Default config check:
  - `./pdp1184 -check-config`
- Explicit RAM size:
  - `./pdp1184 --mem-kb 4104 -check-config`
- RT-11 boot:
  - `./pdp1184 -rk disks/rt11v400.dsk -bootrt11`
- RH11 image attach:
  - `./pdp1184 -rh disks/rk07.img`
- RL image attach:
  - `./pdp1184 -rl disks/rl02.dsk`
- UNIX V5 boot:
  - `./pdp1184 -rk disks/unix_v5_rk.dsk -bootrt11`

### Demo: interactive boot menu (`demo/boot_menu`)
- Purpose:
  - interactive loader helper that detects disk/tape controllers and asks what to boot
  - supports `RK11`, `RH11/HK`, `RL11`, `TQ11/TMSCP`
- Build:
  - `make -C lsi11/demo boot_menu.bin`
- Load address:
  - assembled with `org 0100000`
  - run with `-load demo/boot_menu.bin -addr 0100000 -pc 0100000`
- Typical run (`pdp1184`, boot from RH unit 0):
  - `./pdp1184 -rh disks/rt11v5.3/system.dsk -load demo/boot_menu.bin -addr 0100000 -pc 0100000 -ram 128`
- Typical run (`lsi11`, boot from RH unit 0):
  - `./lsi11 -rh disks/rt11v503.dsk -load demo/boot_menu.bin -addr 0100000 -pc 0100000`
- Interaction:
  - select controller key: `R` (RK), `H` (RH/HK), `L` (RL), `T` (TQ)
  - select unit number:
    - `0..7` for `RK/RH/TQ`
    - `0..3` for `RL`
- Notes:
  - menu probes controller CSRs and handles missing devices via bus-error (`000004`) recovery
  - after selection, it installs/copies built-in bootstrap words and transfers control to selected bootstrap

## Device CSR / IRQ table

| Device | CSR address range(s) (octal) | Vector (octal) | Priority (octal) | DONE bit | IE bit | Software clears DONE | Notes |
|---|---|---:|---:|---|---|---|---|
| DL11 (console RX) | `0177560–0177563` (+ optional alias `0176500–0176503`) | `000060` | `4` | RCSR bit 7 | RCSR bit 6 | Read RBUF (`...62`) | RX IRQ does not repeat until RX DONE cleared |
| DL11 (console TX) | `0177564–0177567` (+ optional alias `0176504–0176507`) | `000064` | `4` | TCSR bit 7 | TCSR bit 6 | Write TBUF (`...66`) | TX IRQ does not repeat until TX DONE cleared |
| DZ11 (terminal RX) | `0160100–0160107` | `000300` | `5` | CSR bit 7 (RDONE) | CSR bit 6 (RIE) | Read RBUF (`0160102`) | RX interrupt source selected by `SAE`: silo alarm (`SA`) vs `RDONE` |
| DZ11 (terminal TX) | `0160100–0160107` | `000304` | `5` | CSR bit 15 (TRDY) | CSR bit 14 (TIE) | Write TDR (`0160106`) | Round-robin transmit line selection via CSR `TLINE` + TCR enables |
| LTC (`KW11-L`-compatible, default on `lsi11` and `pdp1184`) | `0177546–0177547` | `000100` | `6` | CSR bit 7 (monitor) | CSR bit 6 | read CSR low byte (`0177546`) | Fixed line clock. On `lsi11`, this models CPU-board integrated LTC logic; on `pdp1184`, J-11 onboard LTC |
| KW11-P (optional override) | `0172540–0172545` | `000100` | `6` | CSR bit 7 (DONE) | CSR bit 6 | write CSR with DONE=0 | Compatibility-only programmable `single/repeat`, `up/down`, rates `100 kHz/10 kHz/60 Hz/external`, ERR in CSR bit 8 |
| RL11 (RL01/RL02) | `0174400–0174407` | `000160` | `5` | RLCS bit 7 (CRDY) | RLCS bit 6 | Start command by clearing CRDY (negative GO) | BAR/DA/MPR registers, BA16/BA17 in RLCS bits 4-5, commands: NO-OP/WCHK/GET STATUS/SEEK/READ HEADER/WRITE/READ |
| TQ11 (TK50 / TMSCP) | `0174500–0174503` | `000260` | `5` | port/ring driven | port/ring driven | host clears via descriptor ownership / UQ init flow | Opt-in controller, enabled by `-tq`, supports units `tq0..tq7`, SIMH `.tap` backend |
| RK11 (RK05) | `0177400–0177417` | `000220` | `5` | RKCS RDY bit 7 | RKCS IDE bit 6 | Start next command (`GO=1`) | Control Reset/Read/Write/Write Check/Read Check/Seek/Drive Reset/Write Lock, RKER hard/soft errors, RKBA+MEX DMA |
| RH11 (RK611-compatible) | `0177440–0177462` | `000210` | `5` | RHCS1 bit 7 | RHCS1 bit 6 | Write RHCS1 with GO | READ/WRITE/SEEK + RK611 command subset, RHBA + BA16/BA17 DMA |
| LP11 (printer) | `0177514–0177517` | `000200` | `4` | CSR bit 7 | CSR bit 6 | Write DBR (`0177516`) | Output to host stdout |
| SR (switch reg) | `0177570–0177571` | — | — | — | — | — | Read-only 16-bit value, default `000000` |

## Build
- Build both profiles:
  - `make -C lsi11`
- Build only 11/04-like profile:
  - `make -C lsi11 lsi11`
- Build only 11/84-like profile:
  - `make -C lsi11 pdp1184`

## Tests
- 11/04-like tests (MMU disabled + DL11 alias checks):
  - `make -C lsi11 test-lsi11`
- 11/84-like tests (RAM sizing + core MMU suite):
  - `make -C lsi11 test-pdp1184`
- Full matrix:
  - `make -C lsi11 test-matrix`

## Common debug options
- `-trace` instruction trace
- `-trace-regs` trace with registers
- `-traceirq` IRQ delivery trace
- `-tracenxm` bus/NXM trap trace
- `-diag` enable lsi11 diagnostics (same as `LSI11_DIAG=1`)
- `-exit-on-abort` terminate emulator loop when core sets HALT/abort

## Diagnostics for RT-11 XM hang
- Diagnostics are **off by default**.
- Enable with environment:
  - `LSI11_DIAG=1 ./pdp1184 -rk disks/rt11v5.3/SYSXM.DSK -bootrt11 -ram 128`
- Or with CLI flag:
  - `./pdp1184 -diag -rk disks/rt11v5.3/SYSXM.DSK -bootrt11 -ram 128`

### D1: RK11 DMA range + command summary
- On each command start:
  - `RK11 start: PC=%06o RKCS=%06o RKWC=%06o RKBA=%06o RKDA=%06o`
- On each command completion:
  - `RK11 done : PC=%06o RKCS=%06o RKWC=%06o RKBA=%06o RKDA=%06o words=%06o BA=[%06o..%06o] err=%06o irq=%o ferr=%06o`
  - `ferr` is first RK error bit seen during this command (`000000` if none).
- Interpretation:
  - If BA range goes above `0400000` with `-ram 128`, RK11 DMA addressing is wrong.
  - If BA range stays low/sane but boot hangs, look at MMU/IRQ diagnostics.

### D2: MMU enable snapshot (one-shot)
- First `SSR0` MMU-enable transition `0->1` prints:
  - `MMU enable: PC=%06o PS=%06o SSR0=%06o SSR1=%06o SSR2=%06o`
  - `MMU K I%o: PAR=%06o PDR=%06o` (segments `0..7`)
  - `MMU K D%o: PAR=%06o PDR=%06o` when separate I/D is enabled for kernel.
- Interpretation:
  - MMU enable immediately followed by stall usually points to mapping/fault setup.

### D3: WAIT/PC stall detector with IRQ visibility
- On stall:
  - `STALL: reason=<WAIT|PC-LOOP> PC=%06o PS=%06o IR=%06o ...`
  - `IRQS: DLrx=%o DLtx=%o KW=%o RK=%o`
  - `IRQNEXT: vec=%06o pri=%o masked=%o` (when pending)
- Interpretation:
  - `reason=WAIT` and all IRQ pending bits `0`: missing IRQ generation.
  - `reason=WAIT` with IRQ pending but no progress: IRQ accept/priority path issue.
  - Continuous `DLtx=1` with repeated `vec=000064`: DL11 TX latch regression.

## RT-11 XM hang diagnostics (D4/D5/D6)
- Same enable switch:
  - `LSI11_DIAG=1 ./pdp1184 -rk disks/rt11v5.3/SYSXM.DSK -bootrt11 -ram 128`
  - `./pdp1184 -diag -rk disks/rt11v5.3/SYSXM.DSK -bootrt11 -ram 128`
- Reports are rate-limited (periodic summary, no per-instruction spam).

### D4: PC hotspot sampler
- Periodic output:
  - `PC HOTSPOTS (last %06o samples):`
  - `PC=%06o cnt=%06o PS=%06o mode=%o pri=%o`
  - `NOW: PC=%06o PS=%06o IR=%06o`
- Context dump around hottest PC:
  - `PC CONTEXT center=%06o`
  - `  %06o: %06o <disassembly>`
- Interpretation:
  - One dominant PC: tight polling loop.
  - Two alternating dominant PCs: handler/return loop.

### D5: vector histogram (interrupt focus)
- Periodic output:
  - `VEC HIST: KW11(000100)=%06o`
  - `VEC TOP:`
  - `VEC=%06o cnt=%06o` (top non-KW11 vectors)
  - `TRAP HIST: ABORT=%06o MMUFAULT=%06o`
- Per-event (rate-limited) abort line:
  - `ABORT: PC=%06o PS=%06o IR=%06o SSR0=%06o SSR1=%06o SSR2=%06o`
- Interpretation:
  - High non-KW11 vector counts indicate abnormal interrupt activity.
  - Rising `ABORT`/`MMUFAULT` indicates trap/fault churn after banner.

### D6: MMU SSR change/fault logging
- Any `SSR0` transition:
  - `SSR0: PC=%06o PS=%06o %06o -> %06o (SSR1=%06o SSR2=%06o)`
- `SSR3` transition (separate I/D control visibility):
  - `SSR3: PC=%06o PS=%06o %06o -> %06o`
- Kernel map updates (rate-limited):
  - `PAR: mode=K I seg=%o %06o -> %06o`
  - `PDR: mode=K I seg=%o %06o -> %06o`
  - `PAR: mode=K D seg=%o %06o -> %06o`
  - `PDR: mode=K D seg=%o %06o -> %06o`
- MMU fault event:
  - `MMU FAULT: PC=%06o PS=%06o SSR0=%06o SSR1=%06o SSR2=%06o VA=%06o`
- Interpretation:
  - Repeated SSR0 fault-bit transitions with similar `SSR1/SSR2` usually indicate MMU fault loop.
  - `SSR3` enabling split I/D after banner can explain XM-only hangs.
  - Frequent PAR/PDR churn after banner suggests active remapping path; no churn suggests stable map and points to non-MMU logic.

### RAM flag watchpoints (XM non-progress loops)
- Enabled by the same diagnostics switch:
  - `LSI11_DIAG=1 ./pdp1184 -rk disks/rt11v5.3/SYSXM.DSK -bootrt11 -ram 128`
  - `./pdp1184 -diag -rk disks/rt11v5.3/SYSXM.DSK -bootrt11 -ram 128`
- Watched physical RAM addresses (octal):
  - `125726`
  - `125730`
  - `147246`
- Write watchpoint (CPU and DMA writes through bus path):
  - `WPW <B|W> addr=%06o <= %06o PC=%06o PS=%06o mode=%o pri=%o`
- Read watchpoint (filtered to known hot loop PC windows):
  - `WPR <B|W> addr=%06o -> %06o PC=%06o PS=%06o mode=%o`
- Periodic snapshot:
  - `FLAGSNAP PC=%06o PS=%06o 125726=%06o 125730=%06o 147246=%06o`
- Interpretation:
  - No `WPW` after banner and static `FLAGSNAP`: producer path for flags is not running or writes elsewhere.
  - `WPW` present but `FLAGSNAP` unchanged at consumer time: address-space/banking mismatch candidate.
  - `WPR` repeatedly reading stale values in hot loop with no intervening `WPW`: confirms non-progress wait condition.

### D8: VA->PA trace and mirror experiment
- VA->PA trace is enabled with diagnostics:
  - `LSI11_DIAG=1 ./pdp1184 -rk disks/rt11v5.3/SYSXM.DSK -bootrt11 -ram 128`
- Optional verbose trace:
  - `LSI11_DIAG_VERBOSE=1` (removes default `XLT` line cap)
- Optional mirror experiment (explicit behavior change):
  - `LSI11_MIRROR_FLAGS=1`
  - On each word write to `PA=147246`, emulator also writes same word to `PA=125730`.
- `XLT` line format:
  - `XLT <R|W><B|W> PC=%06o PS=%06o mode=%o [space=I|D] VA=%06o -> PA=%06o val=%06o`
- Periodic correlation summary:
  - `XLT SUM: VA125726->PA %06o cnt=%06o | VA125730->PA %06o cnt=%06o | PA147246 writes=%06o reads=%06o`
  - If multiple PAs are seen for one VA, summary appends `(+N others)`.
- Mirror log line:
  - `MIRROR: PA147246->PA125730 val=%06o PC=%06o`
- Interpretation:
  - If `VA125730` maps to `PA147246` but consumer still reads `0`: likely read/write space or bank selection mismatch.
  - If `VA125730` maps to `PA125730` while producer writes `PA147246`: producer/consumer use different physical locations.
  - If `LSI11_MIRROR_FLAGS=1` makes XM reach `.`: mapping mismatch is confirmed.

### D9: microtrace + SIMH oracle comparison
- Enable microtrace in our emulator:
  - `LSI11_MICROTRACE=1 ./pdp1184 -rk disks/rt11v5.3/SYSXM.DSK -bootrt11 -ram 128`
- PC windows (octal):
  - `W1: 134442..134474`
  - `W2: 147234..147264`
- Per-instruction line:
  - `MT I SEQ=%06o PC=%06o IR=%06o PS=%06o mode=%o pri=%o R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o`
- Per-memory-access line (for active window instruction `SEQ`):
  - `MT M SEQ=%06o <R|W><B|W> VA=%06o -> PA=%06o val=%06o`
- Automatic stop:
  - Max `003720` instructions per window (2000 decimal), then prints:
    - `MT SUMMARY`
    - last `000024` `MT I` lines for each window
    - VA/PA read/write counts for `125726`, `125730`, `147246`

- SIMH oracle run:
  - `tools/run_simh_xm_trace.sh /tmp/xm_simh_mt.log`
  - Uses `tools/simh_xm_trace.ini` with:
    - `SET CPU 11/84`
    - `SET CLK 50HZ`
    - breakpoints at `134442` and `147234`
    - `SHOW CPU HISTORY`

- End-to-end compare:
  - `tools/run_xm_microtrace.sh /tmp/xm_our_mt.log`
  - `tools/run_simh_xm_trace.sh /tmp/xm_simh_mt.log`
  - `python3 tools/d9_compare.py --our-log /tmp/xm_our_mt.log --simh-log /tmp/xm_simh_mt.log --max-steps 2000`
- Compare output:
  - `W1 FIRST MISMATCH ...` / `W2 FIRST MISMATCH ...`
  - `FIRST DIVERGENCE WINDOW=... STEP=... REASON=...`

### D10: IRQ/RTI context trace (pre/post) + vector fetch tagging
- Enable:
  - `LSI11_IRQTRACE=1 ./pdp1184 -rk disks/rt11v5.3/SYSXM.DSK -bootrt11 -ram 128`
- Optional stop on known divergence signature:
  - `LSI11_STOP_ON_DIVERGE=1`
- Can run together with D9:
  - `LSI11_MICROTRACE=1 LSI11_IRQTRACE=1 ...`

- IRQ offer (poll path):
  - `IRQ OFFER id=%06o PC=%06o PS=%06o SP=%06o vec=%06o pri=%o src=%s`
- Vector table reads for offered IRQ id:
  - `IRQ VECFETCH id=%06o addr=%06o -> %06o (vec=%06o src=%s)`
  - `addr=vec` is fetched handler PC, `addr=vec+2` is fetched PSW.
- IRQ accept (instruction-boundary correlation):
  - `IRQ ACCEPT id=%06o prePC=%06o prePS=%06o preSP=%06o -> PC=%06o PS=%06o SP=%06o vec=%06o src=%s`
- RTI pre/post:
  - `RTI PRE depth=%o id=%06o PC=%06o PS=%06o SP=%06o`
  - `RTI POST depth=%o id=%06o PC=%06o PS=%06o SP=%06o`
- Window correlation markers:
  - `WIN W1 ENTER PC=%06o PS=%06o SP=%06o depth=%o lastid=%06o`
  - `WIN W2 ENTER PC=%06o PS=%06o SP=%06o depth=%o lastid=%06o`

- Rate limit:
  - max `011610` lines (`5000` decimal), then:
    - `IRQTRACE AUTO-OFF (limit)`
- Optional divergence stop:
  - if `LSI11_STOP_ON_DIVERGE=1` and signature matches:
    - `DIVERGE SIGNATURE HIT PC=%06o PS=%06o SP=%06o`

- Interpretation:
  - If `IRQ VECFETCH` values already differ from reference: vector table content/read path issue.
  - If `IRQ VECFETCH` matches but `IRQ ACCEPT` post state is wrong: interrupt entry/stacking/PSW transfer issue.
  - If `IRQ ACCEPT` is correct and mismatch appears on `RTI POST`: RTI restore path (PC/PSW/SP) is likely wrong.

## CPU/Emulator Documentation Boundary
- CPU core semantics, MMU behavior, trap/IRQ frame rules and core test matrix are documented in [`core/README.md`](../core/README.md).
- This document (`lsi11/README.md`) covers machine integration only: buses, devices, CLI, boot flows, and emulator diagnostics.

## Recent PDP-11/84 Fix (ULTRIX boot path)
- Fixed a `pdp1184` boot regression where `j11_probe_shadow` incorrectly aliased the low I/O page window `0160000..0177777` into probe-shadow logic.
- Correct behavior now:
  - probe-shadow accepts only real high I/O aliases (`0760000..0777777` and `017760000..017777777`)
  - normal RAM/code addresses (for example physical `00172000`) are no longer intercepted
- User-visible impact:
  - ULTRIX-11 boot proceeds to setup (`Load device ...` -> `ULTRIX-11 Kernel V3.1` -> setup prompt) instead of trapping on illegal instruction during early init.

## Device disable options
- `-disable-dl` disable DL11
- `-disable-dz` disable DZ11
- `-disable-kw` disable all KW11 variants
- `-enable-kw11-l` enable KW11-L decode (`0177546..0177547`)
- `-disable-kw11-l` disable KW11-L decode
- `-enable-kw11-p` enable KW11-P decode (`0172540..0172545`)
- `-disable-kw11-p` disable KW11-P decode
- `-disable-lp` disable LP11
- `-disable-rk` disable RK11
- `-disable-rh` disable RH11
- `-disable-rl` disable RL11
- `-disable-sr` disable SR register

Notes:
- If a device is disabled, attaching media for it (for example `-disable-rl` with `-rl`) is rejected.
- By default, both `lsi11` and `pdp1184` enable the onboard `LTC`
  (`KW11-L`-compatible); `KW11-P` is off unless explicitly enabled. The
  `-enable-kw11-*` / `-disable-kw11-*` options override that default.
- `-check-config` prints final per-device enable state (`dev_*` fields),
  including `dev_kw11_l` and `dev_kw11_p`.
