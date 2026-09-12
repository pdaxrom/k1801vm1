# uJ11 FPGA

**CP58a: STEP и SEL004, полный HC1200 synthesis прошёл.**
91 CPU-сценарий × 3 режима ROM/декодера, 12 full-board сценариев × 2,
FIS и cold RT-11FB проходят. Занято **1002/1024** слова микрокода.
**1246 LUT / 342 FF / 6 EBR / Fmax 30,743 МГц**, свободны 34 LUT / 13 slices.
Выбран CP58a; вариант с одним словом SEL004 оказался дороже на 12 LUT.
Плата остаётся CP56a; timing CP58 проверен при номинальных 29,56 МГц,
до установки нужен учёт допуска OSCH и внешних FRAM-сигналов.
[Семантика, измерения и ограничения](docs/service-bank-cp58.md).

**CP57e: первый служебный банк FRAM без MMU, отдельный профиль.**
Полный HC1200: **1260 LUT / 342 FF / 6 EBR / Fmax 31,982 MHz**,
1000/1024 слов микрокода. Тестовая firmware загружается инструкциями CPU
в верхнюю FRAM; работают FP11/HALT entry, START и межбанковый доступ.
Обычная ISA/FIS, benchmarks и cold RT-11FB сохранили результаты CP56.
Запас всего **20 LUT / 5 slices**; это экспериментальный checkpoint,
не новая установленная прошивка. Полные FP11/ODT, RT-11 `.SAV` loader,
полный HALT fault path ВМ2 ещё впереди; STEP/SEL004 проверяются в CP58.
[ABI, результаты и ограничения CP57](docs/service-bank-cp57.md).

**CP56: ускоренный SPI FRAM, полный synthesis PASS.**
**1184 LUT / 339 FF / 6 EBR / 31,996 MHz**. SCK 14,78 → 29,56 MHz
при прежней CPU 29,56 MHz; R,R workloads быстрее в **1,70×**, cold
RT-11FB + DIR — в **1,665×** по simulation. Дополнительный FRAM TRACE
прошёл при заданных PCB budgets, но консервативный запас SCK pulse width
всего **0,147 ns**. CP56a установлен на плату для аппаратной проверки.
[Результаты, предел timing и воспроизведение](docs/spi-cp56.md).

**На плате CP56a:** FLASH Erase/Program/Verify прошли, RT-11FB V05.03
загрузилась до приглашения монитора. Picocom возобновлён; пользовательская
проверка программ и периферии ожидается. [Журнал установки и JED](docs/board-bringup-cp56a.md).
Предыдущие проверки RGB/HDSP/keyboard/HG относятся к
[CP29a](docs/board-bringup-cp29.md).

Специализированный микрокодный PDP-11/J-11 integer engine для
**Lattice LCMXO2-1200HC**, с FIS и 128 КиБ SPI FRAM.

**Основа CP56 — CP54b**, выбранная пользователем после CP55:
1185 LUT / 341 FF / 6 EBR, Fmax **32,273 MHz**, запас **2,843 ns** при
штатных 29,56 MHz. Профиль — `--ack-cp54 dma-ack`. Свободны 95 LUT /
45 slices / 1 EBR; до <=1100 LUT нужно убрать 85 LUT. CP55 сохранён
как эксперимент; default сборки пока CP52a, на плату установлен профиль
`--spi-cp56` с ускоренным контроллером FRAM поверх CP54b.

**CP55: shared FRAM RX, полный synthesis PASS.**
**1182 LUT / 333 FF / 6 EBR / 30,827 MHz** — на 3 LUT и 8 FF меньше CP54b,
но Fmax ниже на 1,446 MHz. Свободны **98 LUT / 47 slices / 1 EBR**;
slack 1,390 ns при 29,56 MHz. Formal, negative/X/Z/reset/128 КиБ tests,
portable/vendor board и cold RT-11FB + DIR сохранили counters/raw UART.
По решению пользователя shared RX из CP55a не принимается в основу:
экономия 3 LUT не оправдывает уменьшение запаса timing относительно CP54b.
[Контракт, измерения и ограничения](docs/rx-cp55.md).

**CP54: оба full-board synthesis PASS; выбран CP54b.**
**1185 LUT / 341 FF / 6 EBR / 32,273 MHz**, −10 LUT от CP53a.
Остаются **95 LUT / 45 slices / 1 EBR**, slack 2,843 ns при 29,56 MHz.
Formal/negative controls, FRAM/CSR, 36 portable/vendor workloads и оба
cold RT-11FB + DIR сохранили counters/raw UART CP53a. Кандидат B сохранён
для дальнейшей оптимизации до цели <=1100 LUT; default и плата прежние.
[Участок критического пути и проверки](docs/ack-cp54.md).

**CP53: три full-board synthesis завершены.** Лучший по площади CP53a:
**1195 LUT / 341 FF / 6 EBR / 31,338 MHz**; −3 LUT относительно CP52b.
Свободны 85 LUT / 38 slices / 1 EBR, timing slack 1,919 ns при 29,56 MHz.
SAT, FRAM/board, vendor EBR и новый cold RT-11FB + DIR прошли;
все counters и UART совпали с CP52. Цель <=1100 LUT ещё не достигнута;
CP53a сохранён для дальнейшей оптимизации, default и плата прежние.
[Исследование и проверки](docs/cursor-cp53.md).

**CP52: sequential FRAM READ прошёл simulation и полный synthesis.**
Регистровые loops: **107 → 40,0625 clocks/instruction (2,671×)**;
холодный RT-11FB + DIR: **354938300 → 288686609 clocks (1,229×)**.
CP52b: **1198 LUT / 341 FF / 6 EBR / 30,044 MHz**; +39 LUT/+15 FF.
Остались 82 LUT, timing slack 0,544 ns при 29,56 MHz. Эксперимент сохранён
отдельно до уменьшения площади; default RTL и плата не менялись.
[Протокол, проверки и результаты](docs/fram-sequential-cp52.md).

**CP51: аудит ресурсов и новые benchmarks MMU-less board.** По измерению
CP40h свободны **121 LUT / 56 slices / 1 EBR**; все PIO sites заняты.
70 свободных microinstructions разбиты на 46 участков, максимум 3 слова.
Новые полные board loops: MOV/ADD/CMP R,R — **107 clocks/instruction**,
FRAM busy 96,26%; скорость прежде всего ограничена SPI memory.
Новая оптимизация RTL ещё не принята. [Ресурсы и направления](docs/resources-cp51.md).

**CP50: рабочая конфигурация HC1200 — MMU-less.** По решению пользователя
от 2026-09-11 дальнейший поиск места под MMU остановлен. В board RTL сохранены
обе ветви `ifdef UJ11_MMU / else / endif`: по умолчанию CP40h (VA=PA, 16 бит),
по явному `MMU=1` — незавершённый CP47c (18/22-bit relocation). MMU-файлы
вообще не входят в default source list; ядро, FIS и 954 слова микрокода общие.
[Сборка, проверки и границы CP50](docs/build-profiles-cp50.md).

```sh
make board                         # MMU=0 по умолчанию
make test-board-profiles            # обе ветви + frozen-source comparisons
make test-board-rt11                # MMU-less cold RT-11FB + DIR
make synthesis-board BOARD_CHECKPOINT=cp50a  # новый Linux/Diamond gate
make board MMU=1                    # сохранить эксперимент, не прошивать HC1200
```

Без MMU CPU адресует нижнее 16-битное пространство; верхние 64 КиБ FRAM
не отображаются. Новое измерение default логики — **CP52a: 1159 LUT /
326 FF / 6 EBR / 31.470 MHz**; полностью повторены ресурсы и Fmax CP40h.
Текущая физическая плата — CP56a; установленный JED и журнал приведены выше.

## История экспериментов до решения CP50

Указанные ниже «следующие этапы» относятся к планам соответствующих
checkpoints; дальнейшая разработка MMU на HC1200 теперь отложена.

**CP49: три FRAM-варианта измерены и отклонены по площади.**
Original encoding / LSB byte skip / bit equations дали **1325 / 1335 /
1310 LUT**, контроль — 1297. Все MAP FAIL, Fmax нет. Original encoding
подтверждён по SRR/EDIF; bit equations перекодированы в 16-bit one-hot.
Formal/unit/CPU/vendor/bus и cold FB split-low прошли, counts/UART прежние.
Основа — CP47c: **1297 LUT / 351 FF / 7 EBR**, превышение 17 LUT / 10 slices.
[Методика и результаты CP49](docs/area-fram-binary-cp49.md).

**CP48: явные переходы FRAM и one-hot проверены и отклонены по площади.**
1319/1313 LUT против 1297 у CP47c; все gates MAP FAIL. Synplify перекодировал
state в one-hot, FF выросли на шесть. Formal, SPI/FRAM, CPU/vendor/bus прошли;
RT-11FB + DIR для successors сохранила counts/UART. Основа — CP47c,
превышение по-прежнему 17 LUT / 10 slices. [Отчёт CP48](docs/area-fram-state-cp48.md).

**CP47: совмещение FRAM RX/high-rdata сэкономило 5 LUT и 8 FF.**
Лучший prototype CP47c: **1297 LUT / 351 FF / 7 EBR / 650 slices**.
Все четыре full-board gates завершились MAP FAIL: до вместимости HC1200
ещё 17 LUT / 10 slices; Fmax отсутствует. Byte mux и combined отклонены
по площади. Formal, X/Z/reset/128 КиБ scoreboard, CPU/vendor/bus и cold
RT-11FB + DIR прошли; counts и UART прежние. Production и плата не менялись.
[Контракт данных и результаты CP47](docs/area-fram-cp47.md).

**CP46: четыре control/PA22 варианта проверены и отклонены по площади.**
1315–1317 LUT против 1302 у CP45k; все MAP FAIL. Functional/formal,
APR vendor, весь верхний банк FRAM и cold RT-11FB + DIR прошли, counts/UART
прежние. Финальные EDIF CP45/CP46 проверены на конфликтующие направленные
драйверы. В CP47 основа CP45 улучшена; RT-11XM ещё не запускалась.
[Результаты CP46](docs/area-control-cp46.md).

**CP45: площадь relocation board уменьшена на 49 LUT.**
Финальный повтор: **1302 LUT / 359 FF / 7 EBR / 652 slices**, MAP FAIL.
До границы HC1200 остаются 22 LUT / 12 slices, затем нужен резерв на
protection/restart. Изменены только bus decode/read mux; CPU/FRAM/vendor
и cold RT-11FB + DIR сохранили все clocks и UART. Выбранная форма прошла
binary и X/Z equivalence. В production не принята, Fmax/XM ещё нет.
[Измерения и проверки CP45](docs/area-bus-cp45.md).

**CP44: CPU relocation 18/22 bits проверен, но не поместился в HC1200.**
CPU записывает/читает весь верхний банк FRAM, portable/vendor/C tests и
cold RT-11FB + DIR прошли. Лучший из пяти gates: **1351 LUT / 359 FF /
7 EBR / 676 slices**, MAP FAIL; Fmax отсутствует. Это kernel unified PAR
prototype без PDR protection/MMR1/2/abort/restart, в production не принят.
До предела устройства нужно убрать минимум 71 LUT / 36 slices, затем
получить резерв на оставшуюся MMU. RT-11XM ещё не запущена.
[Результаты и ограничения CP44](docs/relocation-cp44.md).

**CP43: MMR3 CSR добавлен к экспериментальной APR-сборке.**
Финальный CP43d — **1258 LUT / 351 FF / 7 EBR / 30.866 MHz**, свободны
22 LUT и 10 slices. Это +10 LUT / +7 FF к CP42, без новых microinstructions.
Word/byte, RESET, odd-address trap и isolation от RK DMA проверены;
CPU/vendor, C differential и cold FB + DIR прошли; counts прежние. [Измерения CP43](docs/mmr3-cp43.md).
Production CP40h остаётся **1159 LUT / 326 FF / 6 EBR / 31.470 MHz**.
В принятой CP43-сборке CPU translation отсутствует; CP44 остаётся отдельным
не поместившимся экспериментом. MMR1/2, protection/restart и RT-11XM впереди.

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

Рабочий production CPU **без MMU**, интерфейс 16 bits. Верхние 64 КиБ
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
make synthesis-board BOARD_CHECKPOINT=cp50a # fresh Linux/Diamond implementation
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
APR lookup и CPU CSR проверены в experimental build. MMU, abort/restart
и старшие physical RK DMA addresses отложены решением CP50.
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
