# uJ11 FPGA

**CP29 hardware:** HC1200 FLASH programmed and verified; real RT-11 boot/DIR, user-confirmed RGB/HDSP/keyboard operation and a verified HG read/write round-trip. [Bring-up record](docs/board-bringup-cp29.md).

Специализированный микрокодный PDP-11/J-11 integer engine для
**Lattice LCMXO2-1200HC**, с FIS и 128 КиБ SPI FRAM.

**CP32: проверен отдельный translator 18/22 bits и доступ ко всей SPI FRAM.**
FP11 отложен и удалён из рабочей сборки в CP31.
Удалены FP RTL/state, decode, microcode и build options. Реализация CP30
сохранена в коммите `d59f19c`. Microstore снова **954/1024×36 v12**, свободно
70 слов. ODT также остаётся в [TODO](TODO.md).

| Полный HC1200 top | LUT4 | FF | EBR | TRACE Fmax | 29.56 MHz |
|---|---:|---:|---:|---:|---|
| CP29a, физически прошит | 1239 | 326 | 6 | 30.609 MHz | PASS |
| CP31a, FP11 полностью удалён | 1224 | 326 | 6 | 31.074 MHz | PASS |
| CP31c, RK CSR перенесены из FRAM в firmware EBR | 1252 | 326 | 6 | 30.917 MHz | PASS |

Текущий production CPU **ещё без MMU**, интерфейс 16 bits. Верхние 64 КиБ
FRAM освобождены от служебных RK-регистров, но пока недоступны CPU.
Отдельный translator/PDR checker поддерживает **18 и 22 bits**, PAR16 и
canonical PA22; он ещё не включён в board top. Вход `map22` соответствует
MMR3<4>, сами MMR и таблицы пока отсутствуют. CP32b: **70 LUT / 80 измерительных
FF / 0 EBR / 93.362 MHz**. Это показатели probe, не полного CPU с MMU.
Прошли 36144800 проверок, 262144 сравнений с C MMU и полный проход по
128 КиБ через реальный RTL SPI transport с моделью FRAM.
[MMU: документация, измерения, план интеграции и RT-11XM](docs/mmu.md).
Плату в CP31/CP32 не программировали; физически остаётся CP29.

Работают word/byte integer ISA, все addressing modes, branches, JMP/JSR/RTS/SOB,
SWAB/SXT/MARK, traps/RTI/RTT, trace, IRQ/WAIT/SPL, CC/NOP/MFPT, MFPS/MTPS,
HALT restart, peripheral RESET, ASH/ASHC/XOR/MUL/DIV и FIS. Один kernel register set,
CM=PM=RS=0, NZVC/IPL/T. Память — **MR45V100A SPI FRAM**. Для DIV DEC требует
even R; odd R — документированное расширение. Banking и native ODT отсутствуют.

Реальная RT-11, RGB/HDSP/keyboard/HG проверены в CP29. Текущий CP31c
прошёл cold RT-11FB + DIR в RTL simulation: 354938300 clocks, 3270 UART wire
bytes, 162 SD reads / 6 writes. Это регрессия без MMU, не проверка XM.
Для MMU обязателен пользовательский образ `../lsi11/disks/rt11v5.3/system.dsk`
с RT11XM.SYS; загрузка XM и проверка верхних 64 КиБ ещё впереди.

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
make test-mmu18                   # isolated translation/PDR probe, not integrated MMU
make test-mmu-translate           # 18/22-bit PA/PDR, exhaustive + four-state
make test-mmu-oracle              # existing C MMU, ENABLE_MMU=1
make test-mmu-fram                # translator + SPI transport, complete 128 KiB model
```

Нужны Python 3, Icarus Verilog, Verilator, C compiler и Lattice simulation
models для vendor tests. Yosys нужен для формальной проверки ALU в
`verify-cp28`; [pinned установка и CP23 proof](docs/area-sequencer.md).
Integer oracle собирается из существующего `../core/` с `ENABLE_MMU=0`; проект находится
внутри k1801vm1. Diamond 3.14.0.75.2 настроен на `sash@192.168.1.108`, default
path `$HOME/.local/lscc/diamond/3.14`. `DIAMOND_HOME` и `LATTICE_SIM_DIR` можно
переопределить; локальные vendor tests используют `LATTICE_SIM_DIR=build/vendor`.
Каждый synthesis gate требует свежего implementation directory.

Следующий gate — площадь таблиц PAR/PDR и интеграция MMU с abort/restart
и физическими RK DMA. Полный MMU fit пока не доказан: у CP31c осталось
28 LUT / 12 slices / 1 EBR. Желаемые <=1100 LUT и 50 MHz ещё не достигнуты.

* [CP31/CP32: MMU и использование всей FRAM](docs/mmu.md)
* [CP30: отложенный эксперимент FP11(A)](docs/fp11a.md)
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
