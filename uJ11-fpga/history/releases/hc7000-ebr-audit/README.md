# HC7000 EBR audit

> Историческая запись: `releases/hc7000-ebr-audit/README.md`, Git `31549d1`.
> Прошивки и полные снимки исходников удалены из рабочего дерева; [восстановление](../README.md).


2026-10-01. Isolated synthesis experiments based on `hc7000-serv-shared`.
**Not a board release; no JED and no hardware programming.** Production RTL,
HC1200 and firmware RAM size were not changed by this audit. All prototypes
retain 14 KiB physical SERV/sector memory and the same firmware binary.

The largest passing saving is **five EBR**, obtained with a six-EBR J11 ROM
and distributed RAM for SERV registers and MMU PAR/PDR. The prototype uses
**5689/6864 LUT4, 1791/7209 FF and 21/26 EBR**. CPU Fmax is 51.088 MHz;
PAL Fmax is 64.554 MHz. CPU 50 MHz, PAL 64 MHz, SRAM pin constraints and PAL
mailbox bounds all pass with zero negative slack.

| Experiment | EBR | LUT4 | CPU / PAL Fmax, MHz | Timing |
|---|---:|---:|---:|---|
| `ebr-audit-rom`: dense ROM 39-bit packing | 25 | 5001 | 50.633 / 64.662 | pass |
| `ebr-audit-rf-rom`: packed ROM + distributed SERV RF | 24 | 5297 | 49.133 / 65.172 | **fail** |
| `ebr-audit-apr-rom`: packed ROM + distributed APR | 24 | 5252 | 49.339 / 64.641 | **fail** |
| `ebr-audit-50-01`: packed ROM + both distributed arrays | 23 | 5553 | 51.372 / 67.349 | pass |
| `ebr-audit-sparse`: dense high flags in logic, 7-EBR ROM | 22 | 5589 | 50.800 / 66.432 | pass |
| `ebr-audit-sparse-tail`: dense/tail high flags in logic, 6-EBR ROM | **21** | **5689** | **51.088 / 64.554** | **pass** |

Baseline: 4992 LUT4 and 26 EBR. The final prototype spends 697 extra LUT4 and
leaves 1175. Adding the reclaimed EBRs to SERV RAM is a subsequent change,
requiring its own timing and functional qualification. The proposed layout
is 18 KiB full-width RAM, 512 bytes of cold SERV data and 512 bytes of sector
buffer: **18.5 KiB for firmware/data/stack**. The last 1 KiB bank would be
16 bits wide, so keep the stack in the first 18 KiB.

See the [Russian analysis and integration plan](../../../docs/hc7000-ebr-optimization.md).

`sources/` contains frozen baseline inputs with the two distributed-RAM
overrides and three generated ROM alternatives. `originals/` preserves the
RF/APR implementations used by the comparison projects. Each project's
`synthesis-inputs.json` entry identifies exactly which files it used.
`synthesis/` retains the original project, preferences, logs and
MAP/PAR/TRACE reports. Absolute project paths identify the isolated Linux
build directory; `reproduce.py` relocates them for a fresh run.

`validation/` contains the nine-test MMU suite for the 8-EBR ROM;
`validation-sparse/` checks every address of the 7-EBR ROM;
`validation-sparse_tail/` contains the full suite for the final 6-EBR ROM.
ROM tests perform 8192 checks over all 4096 input addresses and clock-enable
holds, using official Lattice DP8KC/PDPW8KC models. The final suite also
passes decode, translation, MMU transactions/cache, CPU/ODT, CSM, locked bus
and CPU events. `validation/test-inputs/` preserves the exact test sources.
Official simulation models are referenced by hash, not redistributed.

Prepare a fresh Linux synthesis project without a compiler or source rebuild:

```sh
python3 releases/hc7000-ebr-audit/reproduce.py --out /tmp/hc7000-ebr-audit
```

Add `--run` on the Diamond machine to execute MAP/PAR/TRACE. Set
`DIAMOND_HOME` if it differs from `$HOME/.local/lscc/diamond/3.14`.
No bitstream or programming step is executed. Use `--name` to select one
of the comparison experiments above.

Run the frozen CPU/MMU tests with Icarus and the installed official models:

```sh
python3 releases/hc7000-ebr-audit/reproduce.py --out /tmp/hc7000-ebr-tests \
  --validate --vendor-library /path/to/diamond/cae_library/simulation/verilog/machxo2
```

`SHA256SUMS` covers the archived sources, test inputs, results and reports.
