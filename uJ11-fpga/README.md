# uJ11 FPGA

**CP29 hardware:** HC1200 FLASH programmed and verified; real RT-11 boot/DIR, user-confirmed RGB/HDSP/keyboard operation and a verified HG read/write round-trip. [Bring-up record](docs/board-bringup-cp29.md).

Специализированный микрокодный PDP-11/J-11 integer engine для
**Lattice LCMXO2-1200HC**, с FIS и 128 КиБ SPI FRAM.

**CP43: MMR3 CSR добавлен к экспериментальной APR-сборке.**
Финальный CP43d — **1258 LUT / 351 FF / 7 EBR / 30.866 MHz**, свободны
22 LUT и 10 slices. Это +10 LUT / +7 FF к CP42, без новых microinstructions.
Word/byte, RESET, odd-address trap и isolation от RK DMA проверены;
CPU/vendor, C differential и cold FB + DIR прошли; counts прежние. [Измерения CP43](docs/mmr3-cp43.md).
Production CP40h остаётся **1159 LUT / 326 FF / 6 EBR / 31.470 MHz**.
MMR0/1/2, CPU translation и RT-11XM ещё впереди.

CP41 отклонил четыре альтернативы микросеквенсора; его RTL сохранён.
[CP41](docs/area-sequencer-cp41.md), [production CP40](docs/area-datapath.md).

FP11 отложен и удалён из рабочей сборки в CP31.
Удалены FP RTL/state, decode, microcode и build options. Реализация CP30
сохранена в коммите `d59f19c`. Microstore снова **954/1024×36 v12**, свободно
70 слов. ODT также остаётся в [TODO](TODO.md).

| Полный HC1200 top | LUT4 | FF | EBR | TRACE Fmax | 29.56 MHz |
|---|---:|---:|---:|---:|---|
| CP29a, физически прошит | 1239 | 326 | 6 | 30.609 MHz | PASS |
| CP31a, FP11 полностью удалён | 1224 | 326 | 6 | 31.074 MHz | PASS |
| CP31c, RK CSR перенесены из FRAM в firmware EBR | 1252 | 326 | 6 | 30.917 MHz | PASS |
| CP36f, opcode index + word bus | 1222 | 326 | 6 | 31.116 MHz | PASS |
| CP38f, memory read mux | 1188 | 326 | 6 | 30.943 MHz | PASS |
| CP40h, текущая рабочая сборка, datapath/ALU mux | 1159 | 326 | 6 | 31.470 MHz | PASS |

Текущий production CPU **ещё без MMU**, интерфейс 16 bits. Верхние 64 КиБ
FRAM освобождены от служебных RK-регистров, но пока недоступны CPU.
Отдельный translator/PDR checker поддерживает **18 и 22 bits**, PAR16 и
canonical PA22; он ещё не включён в board top. Вход `map22` соответствует
MMR3<4>; CP43 добавляет CSR storage в отдельной сборке, пока без связи с translator. CP32b: **70 LUT / 80 измерительных
FF / 0 EBR / 93.362 MHz**. Это показатели probe, не полного CPU с MMU.
Прошли 36144800 проверок, 262144 сравнений с C MMU и полный проход по
128 КиБ через реальный RTL SPI transport с моделью FRAM.
CP33c добавляет отдельный APR store и CSR decode: **40 LUT / 65 FF /
1 EBR / 96.862 MHz**; из FF только три — состояние контроллера.
Прошли 1144373 команды на portable/vendor EBR, полный PA22 decode и
65536 C differential cases с чтением PAR/PDR перед трансляцией.
[Контракт и ограничения APR](docs/mmu-apr.md).
[MMU: документация, измерения, план интеграции и RT-11XM](docs/mmu.md).
В CP34 полное разделение ALU оказалось дороже: 487 против 471 LUT.
Разделение только PAR add даёт 466 LUT / 173 FF / 0 EBR / 41.315 MHz
в отдельном datapath probe, экономия всего 5 LUT. В production оно не перенесено.
Текущий микрокод позволяет использовать T5–T7 на всех 88 memory words;
это проверено анализом путей и подменой регистров в CPU oracle tests.
Прошли 272917 cases с подменой T5–T7, покрыты все 88 memory uPC.
[Измерения, проверки и следующий эксперимент](docs/mmu-sharing.md).
CP35 сохраняет PSW и CALL link при служебном входе на всех 88 memory words.
Исходный CP35 full board с постоянно включённым hook:
**1273 LUT / 338 FF / 6 EBR / 30.498 MHz**, 963 microinstructions.
Прошли CPU miter, FIS/RAM/SPI FRAM/vendor ROM и cold RT-11FB + DIR.
[Контракт, измерения и проверки CP35](docs/mmu-entry.md).
CP36 упрощает opcode index и ставит byte-lane mux после ответвления opcode
data. Вариант CP36 с CP35 hook занимает **1243 LUT / 338 FF / 6 EBR /
31.107 MHz**, свободны **37 LUT и 14 slices**. В production hook пока не включён:
там свободны 58 LUT и 26 slices. Оба полных RTL board runs сохранили все
прежние counts и UART transcript. MMU ещё не подключён.
[Измерения и проверки CP36](docs/area-decode.md).
CP37d/e проверяет чтение PAR/PDR в T6/T7 через общую ALU: **1265 LUT / 341 FF /
7 EBR / 30.327 MHz**, 963 words, +10 clocks/memory word. Все CPU/FIS/FRAM/vendor
и cold FB tests прошли. Это read-only cost floor без CPU CSR/translation/MMR;
осталось 15 LUT и 5 slices. Production не меняется, XM пока не загружен.
[Эксперимент APR lookup и ограничения](docs/mmu-apr-lookup.md).
CP38 выбирает firmware/FRAM отдельно от малых устройств. С APR lookup теперь
**1228 LUT / 341 FF / 7 EBR / 32.470 MHz**, свободны **52 LUT / 21 slices**.
Production занимает 1188 LUT, свободны 92 LUT / 43 slices / 1 EBR.
Decoder, CPU и ROM images прежние; formal и оба полных FB runs прошли.
[Изменение read mux, реальные fits и проверки](docs/area-board-read.md).
CP39 добавляет CPU CSR с приоритетом physical RK DMA: все 96 PAR/PDR words,
byte lanes, paired W clear и shared-port ownership проверены на portable/vendor
EBR. 69632-case CPU miter и cold FB + DIR прошли; FB counters и UART прежние.
Microstore эксперимента — 963 слова. [Измеренный gate CP39](docs/mmu-apr-csr.md).
CP40 изменяет только operand/writeback mux и ALU result selection. Полная
сборка занимает 1159 LUT, с APR — 1258 LUT; новые FF/EBR/такты не добавлены.
Formal, four-state, CPU/FIS/vendor и оба FB runs прошли.
[Все варианты и границы CP40](docs/area-datapath.md).
Плату в CP31–CP43 не программировали; физически остаётся CP29a.

Работают word/byte integer ISA, все addressing modes, branches, JMP/JSR/RTS/SOB,
SWAB/SXT/MARK, traps/RTI/RTT, trace, IRQ/WAIT/SPL, CC/NOP/MFPT, MFPS/MTPS,
HALT restart, peripheral RESET, ASH/ASHC/XOR/MUL/DIV и FIS. Один kernel register set,
CM=PM=RS=0, NZVC/IPL/T. Память — **MR45V100A SPI FRAM**. Для DIV DEC требует
even R; odd R — документированное расширение. Banking и native ODT отсутствуют.

Реальная RT-11, RGB/HDSP/keyboard/HG проверены в CP29. Production CP40h
прошёл cold RT-11FB + DIR за 354938300 clocks; экспериментальный CP43d
с APR/MMR3 — за 412130048 clocks. В обоих 3270 UART wire bytes и 162 SD
reads / 6 overlay writes. Это регрессии без CPU translation, не проверка XM.
Для MMU обязателен пользовательский образ `../lsi11/disks/rt11v5.3/system.dsk`
с RT11XM.SYS; загрузка XM и проверка верхних 64 КиБ ещё впереди.

```
make all                         # ROM, listing, labels, occupancy, DP8KC lanes
make test                        # full portable regression and lint
make test-fis test-fis-negative
make vendor-fis                  # unmodified Lattice ROM, four simulation shards
make board                      # firmware + synchronous dispatch ROM
make verify-cp28 YOSYS=/path/to/yosys # historical CP28 integration gate
make synthesis-board BOARD_CHECKPOINT=cp36n # fresh Linux/Diamond implementation
make test-reference-core         # existing C core regression
make benchmark-fis               # 8 workloads x3 memory modes
make test-mmu18                   # isolated translation/PDR probe, not integrated MMU
make test-mmu-translate           # 18/22-bit PA/PDR, exhaustive + four-state
make test-mmu-oracle              # existing C MMU, ENABLE_MMU=1
make test-mmu-fram                # translator + SPI transport, complete 128 KiB model
make test-mmu-apr test-mmu-apr-decode test-mmu-apr-oracle
make vendor-mmu-apr LATTICE_SIM_DIR=build/vendor
make test-mmu-dp-sharing MMU_DP_CANDIDATE=1 # full ALU sharing, isolated probe
make test-mmu-dp-sharing MMU_DP_CANDIDATE=2 # relocation only; run sequentially
make test-mmu-dp-negative test-mmu-scratch
make test-decode-compact test-decode-cpu # CP36 index / aligned-word interface
make test-decode-board                  # current production cold RT-11FB
```

Нужны Python 3, Icarus Verilog, Verilator, C compiler и Lattice simulation
models для vendor tests. Yosys нужен для формальной проверки ALU в
`verify-cp28`; [pinned установка и CP23 proof](docs/area-sequencer.md).
Integer oracle собирается из существующего `../core/` с `ENABLE_MMU=0`; проект находится
внутри k1801vm1. Diamond 3.14.0.75.2 настроен на `sash@192.168.1.108`, default
path `$HOME/.local/lscc/diamond/3.14`. `DIAMOND_HOME` и `LATTICE_SIM_DIR` можно
переопределить; локальные vendor tests используют `LATTICE_SIM_DIR=build/vendor`.
Каждый synthesis gate требует свежего implementation directory.

Entry/return с T5–T7 и сохранением PSW/MDR/Q/CALL link проверен.
APR lookup и CPU CSR проверены в experimental build. Следующий gate —
сокращение общей LUT cost перед microcoded translation и MMR, затем
abort/restart и старшие physical RK DMA addresses.
В production свободны 121 LUT / 56 slices / 1 EBR; в CP40 с APR — только
22 LUT / 9 slices / 0 EBR. Полный MMU fit пока не доказан.
Желаемые <=1100 LUT и 50 MHz ещё не достигнуты.

* [CP36: opcode index и word bus, −30 LUT](docs/area-decode.md)
* [CP35: microcode context entry/return](docs/mmu-entry.md)
* [CP34: разделение ALU и scratch lifetime](docs/mmu-sharing.md)
* [CP33: PAR/PDR в EBR и CSR decode](docs/mmu-apr.md)
* [CP31–CP36: MMU и использование всей FRAM](docs/mmu.md)
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
