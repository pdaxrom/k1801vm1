# uJ11 FPGA

**CP29 hardware:** HC1200 FLASH programmed and verified; real RT-11 boot/DIR, user-confirmed RGB/HDSP/keyboard operation and a verified HG read/write round-trip. [Bring-up record](docs/board-bringup-cp29.md).

Специализированный микрокодный PDP-11/J-11 integer engine для
**Lattice LCMXO2-1200HC**, строго **без MMU**, адрес 16 bits.

**CP30: начат FP11(A).** Реализованы CFCC, SETF/SETI/SETD/SETL и
LDFPS/STFPS R0…R7 — семь мнемоник, 21 encoding. Это микрокод uJ11;
постоянное FP-состояние занимает свободные ячейки уже используемого EBR
декодера. Основной RF16×16 сохранён. Полного FP11 и F/D arithmetic ещё нет.

| Полный HC1200 top | LUT4 | FF | EBR | TRACE Fmax | 29.56 MHz |
|---|---:|---:|---:|---:|---|
| CP29a, физически прошит | 1239 | 326 | 6 | 30.609 MHz | PASS |
| CP30d, FP control включён | 1265 | 327 | 6 | 31.284 MHz | PASS |
| CP30e, тот же RTL, FP выключен | 1230 | 326 | 6 | 30.116 MHz | PASS |

Microstore **987/1024×36 v13**, свободно 37 слов. У FP-enabled top осталось
15 LUT / 5 slices / 1 EBR: fit полной FP11 ISA не доказан. Default
`FP11_CONTROL=0`; опция включается явно для текущего checkpoint. ODT отложен
в [TODO](TODO.md). FPGA в CP30 не программировалась; на плате остаётся CP29.

Работают word/byte integer ISA, все addressing modes, branches, JMP/JSR/RTS/SOB,
SWAB/SXT/MARK, traps/RTI/RTT, trace, IRQ/WAIT/SPL, CC/NOP/MFPT, MFPS/MTPS,
HALT restart, peripheral RESET, ASH/ASHC/XOR/MUL/DIV и FIS. Один kernel register set,
CM=PM=RS=0, NZVC/IPL/T. Память — **MR45V100A SPI FRAM**. Для DIV DEC требует
even R; odd R — документированное расширение. Banking и native ODT отсутствуют.

Реальная RT-11, RGB/HDSP/keyboard/HG проверены в CP29. CP30 с включённым FP
прошёл cold RT-11 + DIR в симуляции (355134893 clocks, 3270 UART wire bytes).
[FP11: источники, проверки, synthesis и ограничения](docs/fp11a.md),
[предыдущая интеграция периферии](docs/hc1200-integration.md).

```
make all                         # ROM, listing, labels, occupancy, DP8KC lanes
make test                        # full portable regression and lint
make test-fis test-fis-negative
make vendor-fis                  # unmodified Lattice ROM, four simulation shards
make board                      # firmware + synchronous dispatch ROM
make verify-cp28 YOSYS=/path/to/yosys # historical CP28 integration gate
make synthesis-board BOARD_CHECKPOINT=cp28n # fresh Linux/Diamond implementation
make test-reference-core         # existing C core regression
make benchmark-fis               # 8 workloads x3 memory modes
make test-fp-control vendor-fp-control # experimental FP controls + shared state
```

Нужны Python 3, Icarus Verilog, Verilator, C compiler и Lattice simulation
models для vendor tests. Yosys нужен для формальной проверки ALU в
`verify-cp28`; [pinned установка и CP23 proof](docs/area-sequencer.md).
Integer oracle собирается из существующего `../core/` с `ENABLE_MMU=0`; проект находится
внутри k1801vm1. Diamond 3.14.0.75.2 настроен на `sash@192.168.1.108`, default
path `$HOME/.local/lscc/diamond/3.14`. `DIAMOND_HOME` и `LATTICE_SIM_DIR` можно
переопределить; локальные vendor tests используют `LATTICE_SIM_DIR=build/vendor`.
Каждый synthesis gate требует свежего implementation directory.

Следующий этап FP11 — запас площади/control store, затем addressing modes,
AC transfers и F/D arithmetic. Желаемые <=1100 LUT и 50 MHz ещё не достигнуты.
MMU в эти этапы не входит; ресурсы под него не резервируются.

* [CP30: FP11(A), первое подмножество и измерения](docs/fp11a.md)
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
