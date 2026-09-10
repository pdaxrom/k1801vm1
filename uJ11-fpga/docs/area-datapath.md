# CP40 — выбор операндов и результата datapath

Production уменьшен **1188→1159 LUT4**, сборка с CPU APR CSR и lookup —
**1268→1258 LUT4**. Изменены только `uj11_datapath.v` и `uj11_alu.v`.
Формат/содержимое микрокода, RF16×16, Q, IR/PSW, memory protocol, firmware
и вся периферия сохранены. Новых state bits, EBR и тактов нет.

Финальные HC1200 MAP/PAR/TRACE прошли при 29.56 MHz:

| Scope | LUT4 | FF | EBR | Slices | TRACE MHz | Свободно LUT / slices / EBR |
|---|---:|---:|---:|---:|---:|---|
| [CP40h, production](../synth/reports/cp40h/result.json) | 1159 | 326 | 6 | 584 | 31.470 | 121 / 56 / 1 |
| [CP40i, APR CSR + lookup](../synth/reports/cp40i/result.json) | 1258 | 344 | 7 | 631 | 30.254 | 22 / 9 / 0 |

В production TRACE вырос с 30.943 до 31.470 MHz; с APR уменьшился с 31.075
до 30.254 MHz. В соответствии с приоритетом площади выбран меньший вариант,
проходящий clock constraint. **22 свободных LUT не доказывают fit полной MMU**.
Translation/MMR, автоматический W, abort/restart, CPU PA22 и high RK DMA
не добавлены. Верхние 64 КиБ FRAM пока не доступны CPU, RT-11XM не загружен.
Плата остаётся CP29a, программирования в CP40 не было.

[Verification manifest](verification-cp40.json) связывает raw reports,
source snapshots, tests и выбранные production RTL hashes.

## Что изменено

Трёхбитное поле `pair` по-прежнему задаёт те же операнды ALU:

| Pair | LHS | RHS |
|---:|---|---|
| 0 | A | B |
| 1 | A | Q |
| 2 | A | D |
| 3 | D | B |
| 4 | 0 | B |
| 5 | D | Q |
| 6 | D | A |
| 7 | B | A |

Внутри datapath два двухбитных selector выбирают из этих же четырёх значений.
RF addressing и dynamic Rs/Rd не меняются. В частности, pair5 сохраняет
D/Q для FIS, а pair6 — D/A для MMU helper; предположения D=0 нет.

В writeback сначала выбираются result и два прежних сдвига для RF/Q.
Затем верхний байт либо сохраняется при operand byte write, либо расширяет
знак MOVB. Byte destinations 5/6 не совпадают со shift destinations 3/4,
поэтому эта перестановка сохраняет результат при всех управляющих входах.
Сам Q update, write enable и состояния регистров побайтно прежние.

ALU по-прежнему вычисляет arithmetic, Boolean, left и right paths. Изменён
только выбор результата: arithmetic, затем left, right и Boolean. Carry
chain, Boolean truth table и формулы NZVC не редактировались. Это не
добавление opcode-specific hardware или изменение ISA.

## Все измеренные варианты

Diamond 3.14.0.75.2, LCMXO2-1200HC-4SG32C. A–G имеют всю CP39 APR/peripheral
обвязку. H/I — повторные gates на выбранных рабочих RTL файлах.

| Gate | Datapath | ALU result mux | LUT4 | FF | EBR | Slices | TRACE MHz |
|---|---|---|---:|---:|---:|---:|---:|
| [CP40a](../synth/reports/cp40a/result.json) | Только shift/byte merge | Исходный | 1280 | 344 | 7 | 643 | — |
| [CP40b](../synth/reports/cp40b/result.json) | Только operand pair | Исходный | 1265 | 344 | 7 | 634 | 30.837 |
| [CP40c](../synth/reports/cp40c/result.json) | Оба изменения | Исходный | 1261 | 344 | 7 | 632 | 30.195 |
| [CP40d](../synth/reports/cp40d/result.json) | Оба изменения | Двухбитный selector | 1282 | 344 | 7 | 643 | — |
| [CP40e](../synth/reports/cp40e/result.json) | Исходный | Двухбитный selector | 1278 | 344 | 7 | 642 | — |
| [CP40f](../synth/reports/cp40f/result.json) | Оба изменения | Priority | 1258 | 344 | 7 | 631 | 30.254 |
| [CP40g](../synth/reports/cp40g/result.json) | Исходный | Priority | 1293 | 345 | 7 | 650 | — |
| [CP40h](../synth/reports/cp40h/result.json) | Выбранный RTL, production | Priority | 1159 | 326 | 6 | 584 | 31.470 |
| [CP40i](../synth/reports/cp40i/result.json) | Выбранный RTL, APR | Priority | 1258 | 344 | 7 | 631 | 30.254 |

A/D/E/G не прошли MAP по slices, Fmax не получен. A/E показывают, почему
проверки одного LUT total недостаточно: 1280/1278 LUT укладываются в номинал,
но требуется 643/642 slices при доступных 640. B/C/F/H/I полностью прошли
MAP/PAR/TRACE. Все raw/source archives сохранены, включая неудачные варианты.

Эффекты не складываются: тот же ALU mux отдельно увеличивает площадь,
а вместе с перестройкой datapath даёт меньший полный board. Поэтому результат
выбран по полному MAP/PAR, а не по оценке числа операторов или отдельного блока.
External pin delays не заданы; TRACE не является аппаратным измерением.

## Проверки

- Sequential SAT equivalence для всех трёх datapath variants, без ограничений
  на pair/destination/opcode, reset/enable или данные. Сравниваются outputs
  и переходы из произвольных соответствующих RF/Q states. Исходная ALU
  заморожена из CP39d; это не сравнение двух копий изменённой ALU.
- Отдельная SAT equivalence обеих ALU variants по всем A/B/op/carry/byte
  inputs, включая result и NZVC. Доказательства datapath и ALU композиционно
  покрывают выбранное сочетание. Четыре внесённых дефекта отвергнуты:
  byte merge, Q shift, ADC/SBC carry и перестановка направлений сдвига.
- **266257 cycles для каждого из шести вариантов** в Icarus: unknown RF
  при старте, обычная инициализация через PASS D, все 4096 control combinations,
  изменяющиеся RF/Q/data, hold и reset. Сравниваются outputs и все 16 RF words
  до и после clock edge. Это дополняет двухзначную SAT модель.
- **69632 CPU cases**, все 88 memory words / 8 APR pages, 781100 PAR/PDR reads,
  390550 entry/return pairs, 1607247 held edges, reset в девяти позициях.
  Reference CPU проверяет сохранение guest state при MMU helper; равенство
  старого и нового datapath отдельно доказано выше.
- Portable и vendor CPU CSR tests: **432 readbacks / 720 CSR beats /
  4902 lookup reads / 221988 clocks**, все APR pairs, word/byte, MOVB,
  odd vector4 и сохранность после reset.
- Existing independent FIS oracle corpus: **23840 cases** на RAM и столько
  же через SPI FRAM, включая **3072 injected faults** в каждом run. Vendor
  DP8KC: детерминированная выборка **645 cases / 83 faults**, каждый 37-й
  исходный case. Корпус совпадает с CP37 по SHA256. Новый datapath/ALU
  работает с CP39 shared APR, периодическими helper holds и полным ROM decode.
- Strict `--Wall` lint: default/aligned-word core, datapath и ALU, без waivers.

Обе cold RT-11FB + DIR simulation сохранили counters и UART побайтно:

| Counter | Production | APR |
|---|---:|---:|
| Clocks | 354938300 | 412130048 |
| Retirements | 3984366 | 3987390 |
| Read / write beats | 5217011 / 423616 | 5222610 / 424452 |
| FRAM transactions | 3390712 | 3397976 |
| RK commands | 300 | 300 |
| Timer edges | 576 | 663 |
| UART bytes | 3270 | 3270 |
| SD reads / writes | 162 / 6 | 162 / 6 |

Production microstore **954/1024×36**, APR experiment **963/1024×36**;
прежние +10 clocks/memory word у helper не изменились. FIS и все периферийные
блоки сохранены. Это FB regression, не MMU/XM benchmark. Пользовательский
`lsi11/disks/rt11v5.3/system.dsk` не изменён, SHA256 проверен.

## Воспроизведение

Из `uJ11-fpga`, с Icarus, Verilator и pinned Yosys из
`tools/formal-requirements.txt`:

```sh
python3 tools/build_datapath_mux.py
python3 tools/build_alu_mux.py
python3 tools/check_datapath_mux.py --yosys /path/to/yowasp-yosys
python3 tools/check_alu_mux.py --yosys /path/to/yowasp-yosys
python3 tools/check_datapath_mux_sim.py
python3 tools/check_alu_mux_sim.py
python3 tools/build_mmu_apr_csr.py
python3 tools/check_datapath_mux_cpu.py miter
python3 tools/check_datapath_mux_cpu.py csr
python3 tools/check_datapath_mux_cpu.py csr --vendor
python3 tools/check_datapath_mux_fis.py
python3 tools/check_datapath_mux_lint.py
python3 tools/run_datapath_mux_board.py
python3 tools/run_datapath_mux_board.py --apr
```

Vendor models — неизменённые `build/vendor/{DP8KC,GSR,PUR}.v`, FIS corpus —
`build/fis-vectors.txt`. Финальные gates в свежей Linux/Diamond копии:
`checkpoint_datapath_mux.py cp40h --variant current --production` и
`checkpoint_datapath_mux.py cp40i --variant current`. Для повторения ранних
вариантов использовать их source snapshots: `current` относится к исходникам
на момент измерения. `archive_synthesis.py` и `record_cp40.py` проверяют hashes;
исторические отчёты после изменения inputs не пересоздавать.
