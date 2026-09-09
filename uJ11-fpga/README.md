# uJ11 FPGA

**CP29 hardware:** HC1200 FLASH programmed and verified; real RT-11 boot/DIR, user-confirmed RGB/HDSP/keyboard operation and a verified HG read/write round-trip. [Bring-up record](docs/board-bringup-cp29.md).

Специализированный микрокодный PDP-11/J-11 integer engine для
**Lattice LCMXO2-1200HC**, строго **без MMU**, адрес 16 bits.

**CP28: полный HC1200 top прошёл fit и холодную загрузку RT-11 в симуляции.**
Включены integer/EIS/FIS core, FRAM, KL11/KW11, panel, SD/RK service,
bootstrap, OSCH/reset и реальные pins. MMU отсутствует.

| Scope | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---:|---:|---:|---|---:|
| Полная плата, CP28m, пока без prefetch | **1217** | **318** | **6** | **29.56 MHz PASS** | **31.186 MHz** |
| Исторический CP27a, core + probe | 863 | 299 | 4 | 35 MHz PASS | 35.954 MHz |
| Исторический CP27b, FRAM/prefetch/IRQ + probe | 1095 | 416 | 4 | 29.56 MHz PASS | 31.300 MHz |

RT-11 выполнила DIR: **98 файлов**, 3270 проверенных bytes UART TX,
162 SD reads / 6 writes. Cold boot + startup + DIR: **355132188 clocks**.
Исходный SD image не изменён. Microstore прежний: **954/1024×36 v12**,
349 labels; 70 свободных microinstructions.

До физического предела осталось **63 LUT / 30 slices / 1 EBR**. Желаемые
900–1100 LUT и 50 MHz не достигнуты. Полный top пока без prefetch; external
pin timing и физическая плата не проверены. FP11 не реализован.
[CP28: архитектура, источники, проверки и ограничения](docs/hc1200-integration.md),
[manifest](docs/verification-cp28.json), [UART transcript](tb/reports/cp28/cp28-uart.txt).

Работают word/byte integer ISA, все addressing modes, branches, JMP/JSR/RTS/SOB,
SWAB/SXT/MARK, traps/RTI/RTT, trace, IRQ/WAIT/SPL, CC/NOP/MFPT, MFPS/MTPS,
HALT restart, peripheral RESET, ASH/ASHC/XOR/MUL/DIV и FIS. Один kernel register set,
CM=PM=RS=0, NZVC/IPL/T. Память — **MR45V100A SPI FRAM**. Для DIV DEC требует
even R; odd R — отдельное программное расширение. Banking и native ODT ещё
предстоят; RT-11 проверена в симуляции. FPGA не программировалась.

```
make all                         # ROM, listing, labels, occupancy, DP8KC lanes
make test                        # full portable regression and lint
make test-fis test-fis-negative
make vendor-fis                  # unmodified Lattice ROM, four simulation shards
make board                      # firmware + synchronous dispatch ROM
make verify-cp28 YOSYS=/path/to/yosys # current integration gate
make synthesis-board BOARD_CHECKPOINT=cp28n # fresh Linux/Diamond implementation
make test-reference-core         # existing C core regression
make benchmark-fis               # 8 workloads x3 memory modes
```

Нужны Python 3, Icarus Verilog, Verilator, C compiler и Lattice simulation
models для vendor tests. Yosys нужен для формальной проверки ALU в
`verify-cp28`; [pinned установка и CP23 proof](docs/area-sequencer.md).
Integer oracle собирается из существующего `../core/` с `ENABLE_MMU=0`; проект находится
внутри k1801vm1. Diamond 3.14.0.75.2 настроен на `sash@192.168.1.108`, default
path `$HOME/.local/lscc/diamond/3.14`. `DIAMOND_HOME` и `LATTICE_SIM_DIR` можно
переопределить; локальные vendor tests используют `LATTICE_SIM_DIR=build/vendor`.
Каждый synthesis gate требует свежего implementation directory.

Следующий этап — снижение площади и prefetch в полном top, затем board timing и физическая проверка.
MMU в эти этапы не входит; ресурсы под него не резервируются.

* [CP27: FIS, exact reference и измерения](docs/fis.md)
* [HC1200: интеграция периферии и RT-11](docs/hc1200-integration.md)
* [CP26: DIV, документы, проверки и измерения](docs/eis-div.md)
* [CP25: MUL, исправление эталона и измерения](docs/eis-mul.md)
* [CP24: XOR, проверки и измерения](docs/eis-xor.md)
* [CP23: площадь микросеквенсора](docs/area-sequencer.md)
* [ASHC и исправления ASH/эталона](docs/eis-ashc.md)
* [ASH: алгоритм, alias/flags/fault проверки и измерения](docs/eis-ash.md)
* [HALT/RESET: профиль, периферия, ошибки и измерения](docs/system-control.md)
* [MFPS/MTPS: microcode, FRAM, flags/IRQ и fault tests](docs/psw-transfer.md)
* [CC/NOP/MFPT: microcode, три fit-варианта и проверки](docs/system-flags.md)
* [Trace/RTT: архитектура, проверки и ограничения](docs/trace-rtt.md)
* [Текущий статус](docs/implementation-status.md), [benchmarks](docs/benchmarks.md)
* [Предыдущие эксперименты](docs/previous_experiments.md)
* [Архитектура](docs/architecture.md), [микроархитектура](docs/microarchitecture.md), [microcode format](docs/microcode-format.md)
* [FRAM и периферия lsi11-fpga](docs/fram-peripherals.md)
* [Memory faults](docs/memory-faults.md), [reserved traps](docs/reserved-traps.md), [IRQ/WAIT/SPL](docs/interrupts.md)
* [Software traps/RTI](docs/software-traps.md), [control flow](docs/control-flow.md), [byte ISA](docs/byte-instructions.md)
