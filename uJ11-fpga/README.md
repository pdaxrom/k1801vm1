# uJ11 FPGA

Специализированный микрокодный PDP-11/J-11 integer engine для
**Lattice LCMXO2-1200HC**, строго **без MMU**, адрес 16 bits.

**CP27 завершён: микрокодный FIS — FADD/FSUB/FMUL/FDIV.**
RF 16×16/Q и FRAM transport сохранены;
pair5 расширен до D/Q без новых FF/EBR. Microstore **954/1024×36 v12**;
все 700 слов CP26 остались на своих адресах, свободны 70 слов.

| Измеренный scope | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---:|---:|---:|---|---:|
| Core + probe, CP27a | **863** | 299 | 4 | **35 MHz PASS** | **35.954 MHz** |
| Core + FRAM/prefetch + IRQ adapter + probe, CP27b | **1095** | 416 | 4 | **29.56 MHz PASS** | **31.300 MHz** |

До желательных 1100 осталось **5 LUT**, до физических 1280 —185. Полные
UART/timer/panel/SD/RK и external pin timing в fit не входят. Следующий gate —
[общий top с периферией и bootstrap RT-11](docs/hc1200-integration.md).
50 MHz и 900–1000 LUT ещё не достигнуты.

FIS прошёл **23840 случаев** arithmetic/registers/PSW/trace/IRQ/memory faults
на каждой RAM/FRAM × portable/vendor; эталон использует точные Fraction. 24 FIS benchmarks/ROM
побайтно совпали между portable и Lattice DP8KC. Все 15 намеренных ошибок
отвергнуты. Сохранение старого datapath доказано Yosys; свежая portable
integer-регрессия прошла **268843 instruction cases +49596 fault frames**
на каждой RAM/FRAM. Прежняя полная integer vendor-регрессия и 1146 benchmarks
остаются результатами CP26. [FIS, документы и границы профиля](docs/fis.md),
[manifest](docs/verification-cp27.json), [измерения](docs/benchmarks-cp27.json).

Работают word/byte integer ISA, все addressing modes, branches, JMP/JSR/RTS/SOB,
SWAB/SXT/MARK, traps/RTI/RTT, trace, IRQ/WAIT/SPL, CC/NOP/MFPT, MFPS/MTPS,
HALT restart, peripheral RESET, ASH/ASHC/XOR/MUL/DIV и FIS. Один kernel register set,
CM=PM=RS=0, NZVC/IPL/T. Память — **MR45V100A SPI FRAM**. Для DIV DEC требует
even R; odd R — отдельное программное расширение. Banking, native ODT и
запуск ОС ещё предстоят. FP11 не реализован. MMU отсутствует; FPGA не программировалась.

```
make all                         # ROM, listing, labels, occupancy, DP8KC lanes
make test                        # full portable regression and lint
make test-fis test-fis-negative
make vendor-fis                  # unmodified Lattice ROM, four simulation shards
make verify-cp27                 # current gate, proof, parity and archive (YOSYS required)
make test-reference-core         # existing C core regression
make benchmark-fis               # 8 workloads x3 memory modes
make synthesis                   # CP27a, 35 MHz; Linux host with Diamond
make synthesis-fram              # CP27b, 29.56 MHz; same host
```

Нужны Python 3, Icarus Verilog, Verilator, C compiler и Lattice simulation
models для vendor tests. Yosys нужен для формальной проверки datapath в
`verify-cp27`; [pinned установка и CP23 proof](docs/area-sequencer.md).
Integer oracle собирается из существующего `../core/` с `ENABLE_MMU=0`; проект находится
внутри k1801vm1. Diamond 3.14.0.75.2 настроен на `sash@192.168.1.108`, default
path `$HOME/.local/lscc/diamond/3.14`. `DIAMOND_HOME` и `LATTICE_SIM_DIR` можно
переопределить; локальные vendor tests используют `LATTICE_SIM_DIR=build/vendor`.
Каждый synthesis gate требует свежего implementation directory.

Следующий этап — полный board top с периферией lsi11-fpga и свой resource gate.
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
