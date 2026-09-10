# CP42 — вход D экспериментального APR engine

**Сборка с CPU APR CSR и lookup уменьшена с 1258 до 1248 LUT4.**
Финальный CP42d: **1248 LUT / 344 FF / 7 EBR / 625 slices / 30.896 MHz**,
полный MAP/PAR/TRACE при 29.56 MHz прошёл. Свободны **32 LUT / 15 slices /
0 EBR**. Новых FF, EBR, microinstructions и микротактов нет.

Изменение применяется только генератором экспериментального APR engine.
Production CP40h остаётся **1159 LUT / 326 FF / 6 EBR / 31.470 MHz**:
тот же вариант без APR дал 1160 LUT и поэтому не принят в native RTL.
Плата остаётся CP29a, программирования не было. Полная MMU ещё не подключена;
translation/MMR/PDR checks/automatic W/abort-restart/CPU PA22/high DMA
и RT-11XM остаются следующими задачами. 32 LUT не доказывают их fit.

## Предыдущие эксперименты и выбранная форма

[CP37](mmu-apr-lookup.md) уже сравнил включение APR в ветвь ZERO обычного
selector и отдельное masked добавление. Второй подход уменьшил тогдашнюю
полную схему с 1286 до 1265 LUT. CP42 сохраняет этот способ подключения APR.
[CP34](mmu-sharing.md) также показал, что аппаратное разделение ALU не даёт
большой экономии само по себе. Datapath, ALU и микросеквенсор здесь не меняются.

В прежнем D selector восемь 16-битных источников: ZERO, ONE, TWO, STEP,
MDR, displacement, literal, PSW. Однако для старшего байта остаются только
MDR, PSW и повторённый знак displacement. Выбранный вариант формирует этот
байт отдельно. В младшем байте малые константы выбираются отдельно от
MDR/displacement/literal/PSW. APR по-прежнему добавляется после native D
через `apr_data & {16{mmu_active && D_select==0}}`.

Источники и их кодировки не изменены. STEP равен единице только для byte
и decoded RF A<6; SP/PC и temporaries получают два. Branch displacement
сохраняет знак; при IR<14>=1 сохраняется прежний unsigned SOB offset.
PSW/MDR не урезаются, literal остаётся 8-битным. Вне context D=ZERO остаётся
нулём. Все эти свойства проверяются при произвольных входах, включая
сочетания, отсутствующие в нынешнем микрокоде.

## Полный HC1200 synthesis

Diamond 3.14.0.75.2, LCMXO2-1200HC-4SG32C, constraint 29.56 MHz.

| Gate | Scope / вариант | LUT4 | FF | EBR | Slices | TRACE MHz | Words | Решение |
|---|---|---:|---:|---:|---:|---:|---:|---|
| CP40i | APR, исходный | 1258 | 344 | 7 | 631 | 30.254 | 963 | Baseline |
| [CP42a](../synth/reports/cp42a/result.json) | APR, дерево выбора D | 1253 | 344 | 7 | 629 | 31.554 | 963 | Меньше выигрыш площади |
| [CP42b](../synth/reports/cp42b/result.json) | APR, отдельный high byte | 1248 | 344 | 7 | 625 | 30.896 | 963 | Выбран |
| [CP42c](../synth/reports/cp42c/result.json) | Production, отдельный high byte | 1160 | 326 | 6 | 585 | 31.022 | 954 | +1 LUT к CP40h, отклонён |
| [CP42d](../synth/reports/cp42d/result.json) | Финальный APR генератор | 1248 | 344 | 7 | 625 | 30.896 | 963 | PASS, принят |

Все четыре gates прошли MAP/PAR/TRACE. Перед D убраны неиспользуемые старшие
биты промежуточного displacement-сигнала, выявленные strict lint. Ширина
сигнала теперь 8 bits; архитектурный displacement по-прежнему 16 bits.
Финальный D точно повторил B по LUT/FF/EBR/slices/Fmax, без lint waivers.
A быстрее по TRACE, но B/D меньше по площади и проходит частоту платы.

External pin delays не заданы, TRACE не является аппаратным измерением.
Цена D-mux зависит от полной схемы: B экономит LUT с APR, C добавляет LUT
без APR. Поэтому native production не изменён. Старые raw отчёты и source
archives сохранены; переименование неиспользуемых сигналов не подменяет
измерение финального набора исходников.

## Проверки выбранной сборки

- SAT: четыре доказательства эквивалентности (tree/sliced × production/APR)
  по точным D-cones, извлечённым из компилируемых engine. Reference заморожен
  из CP40i. Все uword/IR, decoded A, byte, MDR/PSW/APR и context-active свободны.
- Три внесённых дефекта обнаружены: byte step у R6, знак/старшие биты SOB,
  утечка APR в остальные источники D.
- Icarus: **135168 сравнений на вариант** — 131072 обычных и 4096 с X в
  selected/unselected operand data. Selectors известны; X на D selector
  не входит в заявленный контракт.
- Strict Verilator `--Wall`: два CPU decode-профиля, engine и datapath.
- CPU miter: **69632 cases**, все **88 memory words**, **781100 PAR/PDR reads**,
  все восемь страниц, reset в девяти позициях helper. 2554664 normal edges,
  5512747 extra edges, 390550 входов/возвратов и 1607247 held edges — как CP40.
- CPU APR CSR на portable/vendor: **432 readbacks / 720 beats / 4902 lookup
  reads / 221988 clocks**, все 48 пар, word/byte/MOVB, vector4 при odd
  word access, отсутствие записи на fault и сохранность APR после reset.
- FIS: **23840 exact cases + 3072 injected faults** на RAM и столько же на
  SPI FRAM; vendor EBR — **645 cases / 83 faults**. Использован прежний
  независимый oracle corpus. Все три cycle CSV побайтно совпадают с CP40.

Microcode по-прежнему 963 слова с APR: 954 guest + 9 helper. Прежняя
надбавка lookup — 10 clocks/memory word без hold. За пределами D-cone
экспериментальный engine побайтно прежний; остальные hardware inputs APR
не изменены. Все аппаратные inputs production совпадают с CP40h.

## RT-11FB, FRAM и границы результата

Новый cold board run с APR использует настоящий RTL SPI FRAM, UART/KW11,
SD/RK bootstrap и неизменённый RT-11FB image. После boot и DIR:

| Метрика | CP42 APR | Сравнение с CP40 APR |
|---|---:|---|
| Clocks | 412130048 | Совпадает |
| Retirements | 3987390 | Совпадает |
| Memory reads / writes | 5222610 / 424452 | Совпадает |
| FRAM transactions | 3397976 | Совпадает |
| RK commands / timer edges | 300 / 663 | Совпадает |
| UART bytes | 3270 | Содержимое побайтно совпадает |
| SD reads / overlay writes | 162 / 6 | Совпадает |

Backing image открывается для чтения; записи идут в RAM overlay модели.
Production cold boot повторно не запускался, поскольку его hardware inputs
прежние. FPGA не перепрограммировалась. Образы FB и XM проверены по SHA256,
не изменены. RT-11XM не загружен, CPU ещё не использует верхние 64 КиБ FRAM.
Это оптимизация уже проверенного APR lookup, а не запуск трансляции.

[Verification manifest](verification-cp42.json) связывает все четыре
synthesis archives, exact source hashes, formal/simulation/CPU/FIS/vendor
и новый cold FB run. Historical CP37/CP40 record scripts не перезапускались.

## Воспроизведение

Из корня `uJ11-fpga`, с существующими generated microcode, FIS corpus,
Lattice simulation models и образами дисков:

```sh
python3 tools/build_d_input_cp42.py
python3 tools/build_mmu_apr_csr.py
python3 tools/check_d_input_cp42.py --yosys /path/to/yosys
python3 tools/check_d_input_cp42_sim.py
python3 tools/check_d_input_cp42_lint.py
python3 tools/check_d_input_cp42_cpu.py miter
python3 tools/check_d_input_cp42_cpu.py csr
python3 tools/check_d_input_cp42_cpu.py csr --vendor
python3 tools/check_d_input_cp42_fis.py
python3 tools/run_d_input_cp42_board.py --apr
python3 tools/checkpoint_d_input_cp42.py cp42d --variant current
```

Gate запускается с Diamond в свежем implementation directory. Для точного
исторического A/B/C использовать их `source.tgz`, включая версии генератора.
Проверенные reports архивирует `archive_synthesis.py`; `record_cp42.py`
проверяет и собирает итоговый manifest. Рабочие файлы `microasm11` не нужны.
