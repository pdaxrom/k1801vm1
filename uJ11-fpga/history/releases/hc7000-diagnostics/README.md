# HC7000 hardware diagnostics, JED 9DF6

> Историческая запись: `releases/hc7000-diagnostics/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


The opt-in MMU display block shows reset, HALT, ODT, WAIT and processor mode,
with CPU/SD activity dots. It scans at most one segment across both digits,
including the dots: 1/64 duty per segment, approximately 500 Hz frames.
All eighteen display pads use DRIVE=4 and SLEWRATE=SLOW. No EBR or SERV
firmware is added. HC1200/MMU-less remains unchanged; the display defaults
to disabled and is enabled with `HC7000_DIAGNOSTICS=1`.

The installed board has no current-limiting resistors. Scanning and DRIVE
reduce aggregate loading but do not impose a peak-current limit. The
functional tests below do not establish electrical safety; peak current
has not been measured.

The production profile is HC7000 MMU, FPP off, SERV storage, 50 MHz from
the 12 MHz oscillator. Routed results: **4385 LUT4, 1511 FF, 26 EBR**,
1162 microcode words, Fmax **50.728 MHz**. Clock and external SRAM timing
pass. Compared with the previous 1B7F release: +146 LUT4 and +70 FF.

`uj11-hc7000-diagnostics-50mhz.jed` is the physically installed **9DF6**:

```
c27e0a63c76e0f4fd8d189ee432dba049931e86c7ce045357f223125373e4f05
```

`synthesis/` retains the input manifest, Diamond strategy, timing/pin/resource
reports and export record. The manifest records original build paths.
`generated/` holds the exact production top-level and microcode ROM whose
hashes match that manifest; their shared build-directory copies were later
overwritten by other profiles. All hand-written synthesis inputs match the
source tree in this commit.

Validation retained in `validation/`:

* Display simulation: 48 complete-frame/duty checks, 192 reset-phase checks,
  and continuous assertions for blanking and one selected segment.
* MMU/CPU/ODT/event regression at 50 MHz without FPP and at 24 MHz with
  microcoded FPP. The latter's exact generated ROM is retained separately;
  the 50 MHz suite uses the ROM in `generated/`.

On the physical board, Flash Erase/Program/Verify passed. The clean UART
retest and user observations confirmed `r5`, `U5`, button ODT `0d`, HALT
`HL`, STEP returning to `0d`, and WAIT `--` with the CPU dot off. Register
readback verified the HALT, INC and WAIT instruction addresses/results.
The BSD secondary bootstrap's `U5` agrees with its PSW `140344`.

After testing, BSD 2.9.1 booted to multiuser root, mounted `/usr` on XP0h,
and completed `pwd`, `ls /usr`, `cat /etc/fstab` and `sync`. `SY`/`SU`, the
SD dot and dot suppression during held reset were not separately observed
on the board. `hardware/retest/session.json` records these limits and the
two resolved harness errors; it reports the final functional pass.

`hardware/programming/` preserves Flash verification and the initial UART
capture, which had a competing terminal and is not the clean functional
test. The repeat capture is under `hardware/retest/`. Host scripts are
retained for provenance and contain bench-specific paths.

See [wiring, codes, build options and test details](../../../docs/hc7000-diagnostics.md).
`SHA256SUMS` covers every packaged file except itself.
