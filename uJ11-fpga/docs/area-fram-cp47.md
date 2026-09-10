# CP47 — SPI FRAM: −5 LUT / −8 FF, HC1200 ещё переполнен

**Лучший вариант CP47c `shared-rx`: 1297 LUT / 351 FF / 7 EBR / 650 slices.**
Совмещение FRAM RX с верхним байтом результата уменьшило полную сборку
на **5 LUT / 8 FF / 2 slices** относительно CP45k. Все четыре synthesis
gates завершились MAP FAIL: лучший вариант превышает HC1200 на **17 LUT /
10 slices**. PAR/TRACE/Fmax отсутствуют. FPGA не программировалась.

CP47c сохранён как лучшая экспериментальная основа для следующих area
checkpoints. CP45k остаётся неизменным контролем: **1302 LUT / 359 FF /
7 EBR / 652 slices**. Production CP40h, принятый APR experiment CP43d и
физическая плата CP29a не менялись. Запаса под protection/restart ещё нет.

## Кандидаты

`tools/build_fram_cp47.py` проверяет hash исходного transport относительно
CP45k и создаёт отдельные копии в `build/cp47-fram/`. Для полного board
используется CP45 `narrow-rom`, а не отклонённые CP46 control changes.
Native CPU, ALU, MMU bridge/APR, firmware, остальные устройства и microcode
**954/1024×36 v12** не меняются.

| Вариант | Изменение | Статус |
|---|---|---|
| `baseline` | Исходный board FRAM transport | Контроль CP47a воспроизвёл площадь CP45k |
| `byte-mux` | Два последовательных выбора word/byte вместо четырёх отдельных payload cases | Formal + simulation PASS, отклонён по площади |
| `shared-rx` | Приёмный shift register совмещён с верхним байтом `rdata` | Лучший area prototype; formal/CPU/vendor/bus/cold FB PASS по контракту ниже |
| `combined` | Оба преобразования | Formal/CPU/vendor/bus/cold FB PASS, отклонён по площади |

В HIGH=5, LOW=6, DATA_LO=7, DATA_HI=8 слово выбирается через
`state[0] ~^ state[1]`, байт — через `state[1]`. Это используется только
в этих четырёх case branches. WREN, READ/WRITE command, bank byte и default
сохраняются отдельно. Conditional mux сохраняет выбранные X/Z bits.

В `shared-rx` каждый SPI rising edge сдвигает MISO в `rdata[15:8]`.
После DATA_LO этот байт копируется в `rdata[7:0]`; byte transfer также
обнуляет high byte. После DATA_HI high byte уже содержит полученный байт.
Состояния, счётчики, reset, WREN/CS/SCK/MOSI, `ready/error/busy` и задержки
не изменяются. Bank input по-прежнему передаёт физический FRAM A16;
классификация полного PA22 выполняется upstream, до сужения адреса.

## Измерения полной сборки

Diamond **3.14.0.75.2**, **LCMXO2-1200HC-4SG32C**, constraint 29.56 MHz;
внешние pin delays не заданы. Native CPU, вся board-периферия и partial
relocation включены. В каждом варианте microstore — **954 words**.

| Gate | Вариант | LUT4 | FF | EBR | Slices | Fmax | Результат |
|---|---|---:|---:|---:|---:|---:|---|
| [CP47a](../synth/reports/cp47a/result.json) | baseline | 1302 | 359 | 7 | 652 | — | MAP FAIL |
| [CP47b](../synth/reports/cp47b/result.json) | byte-mux | 1326 | 359 | 7 | 665 | — | MAP FAIL |
| [CP47c](../synth/reports/cp47c/result.json) | shared-rx | 1297 | 351 | 7 | 650 | — | MAP FAIL |
| [CP47d](../synth/reports/cp47d/result.json) | combined | 1317 | 351 | 7 | 660 | — | MAP FAIL |

Контроль CP47a совпал с CP45k по LUT/FF/EBR/slices. Два последовательных
mux ухудшили mapping: +24 LUT отдельно и +20 LUT относительно shared-rx
в combined. Поэтому byte-mux не переносится в выбранный prototype.
Сохранены raw reports, input manifests и source snapshots четырёх gates;
hash каждого входного файла проверен относительно текущих исходников.

В иерархическом Synplify netlist FRAM число `ORCALUT4` изменилось
82 → 91 → 73 → 80 для a/b/c/d, `PFUMX` — 9 → 0 → 9 → 1.
Это не независимые MAP LUT costs: полная сборка включает packing и
оптимизацию между блоками. Выбор основан на полной таблице выше.
Заданная частота не является измеренным Fmax, а уменьшение LUT само по
себе не доказывает улучшения critical path.

## Контракт результата

**`shared-rx` и `combined` намеренно меняют high `rdata` во время busy.**
Правильное полное значение гарантируется на `ready`, остаётся стабильным
в idle и совпадает с baseline также в DONE до ACK. Low byte совпадает
каждый такт. Для потребителя, требующего неизменного полного `rdata` во
время передачи, такая замена не подходит.

Проверен настоящий путь board → CPU. Board ACK использует `fram_ready`;
engine захватывает read data в MDR на завершении memory operation, а opcode
и decode ROM — на `fetch_capture`, квалифицированном request/ACK/no-error.
Промежуточное изменение `mem_read_data` не разрешает запись IR/MDR/ROM.
Исходники engine/decode/memory включены в verification manifest.
Это проверяется также полными CPU и cold-board прогонами ниже.

## Formal и unit tests

`tools/check_fram_cp47.py` выполняет шесть positive proofs: три варианта
при CLK_DIV=1 и 3. `byte-mux` сравнивается со всеми исходными outputs
каждый такт. Для shared receive применяется SAT temporal induction с
инвариантами, доказываемыми вместе с outputs:

- совпадают control state, counter/divider, TX, seen и все SPI/handshake pins;
- low `rdata` совпадает всегда, high — на ready, в IDLE/DONE;
- уже принятые младшие bits RX совпадают по маске числа sampled bits;
- state остаётся допустимым, active разрешён только в serial states.

Proof начинает с reset, затем все входы, включая последующие reset,
произвольны. Маска и debug outputs существуют только в formal harness,
в синтезируемый RTL не входят. Первоначальное сравнение без инварианта
частично принятого RX не смогло доказать shared-state преобразование;
усиление индукционного утверждения позволило доказать тот же контракт.
Это не ограничение MISO или последовательности команд assumption-ами.

Две намеренные ошибки — bank alias и неверный high byte byte-result —
отклоняются formal. Отдельный executable miter также обнаруживает обе
ошибки с настоящими counterexamples, а не только отказом proof.

`tools/check_fram_cp47_units.py` выполняет **18 прогонов**: для каждого
варианта и CLK_DIV=1/2/3 — четыре-состояния miter и независимый FRAM model
со scoreboard. На каждый вариант:

| CLK_DIV | Miter beats | Reset offsets | Сравнения тактов | Memory transactions |
|---:|---:|---:|---:|---:|
| 1 | 128 | 128 | 20561 | 2048 |
| 2 | 128 | 256 | 55441 | 2048 |
| 3 | 128 | 384 | 106705 | 2048 |

Итого по трём вариантам: **548121** сравнение тактов, **2304** reset offsets,
**18432** memory transactions. Reset sweep прерывает word/byte writes в
разных фазах; остальные beats включают reads/writes. Miter подаёт X/Z на
MISO и payload при известных control/address. Памятный scoreboard проверяет
оба банка, byte/word/odd, отсутствие повторного ACK при held request и
сравнивает все **128 КиБ** модели после операций. Strict Verilator lint
трёх generated RTL проходит без предупреждений.

## Полный CPU и board

Каждый из вариантов **shared-rx и combined** прошёл следующие проверки
с теми же исходниками, которые использованы в CP47c/d synthesis:

| Проверка | Результат |
|---|---|
| CPU portable: 4096 words × 8 upper pages × 18/22 | 67468380 clocks, 590096 PAR reads, 8 control beats |
| CPU vendor EBR: 4 words/page/mode | 97692 clocks, 848 PAR reads, 8 control beats |
| Portable/vendor edge cases | Wrap/NXM/I/O, MOVB lane/sign, odd vector4, high mapped stack/opcode/immediate — PASS |
| Full bus | 32 transaction checks, 6291456 PA/private-bypass combinations — PASS |
| Cold RT-11FB + DIR | 415159611 clocks, counts и UART совпали с CP45 |

CPU записал и прочитал каждое слово верхних 64 КиБ FRAM; snapshot нижнего
банка сохранился. Cold FB: 4311823 retirements, 5675704 reads / 489280 writes,
3980328 FRAM transactions, 300 RK commands, 576 timer edges, 3270 UART bytes,
162 SD reads / 6 RAM-overlay writes. Mapped/high-FRAM beats — 526239/65664,
MMR0 writes — 4. Clocks и UART byte-identical. Это simulation results,
аппаратные instructions/sec и новый Fmax не измерены.

MMU-enabled private ROM/DMA beats — 0. Active-MMU RK/high DMA и RT-11XM
не проверены. FIS corpus отдельно не повторялся: CPU/ALU/microcode прежние.
APR port RTL не менялся, его большой standalone corpus также не повторялся;
CSR и lookup задействованы CPU/board тестами. Vendor compile сохраняет
прежние предупреждения DP8KC/reference UART; portable builds чистые.

Оба backing disk image не изменены, включая
`../lsi11/disks/rt11v5.3/system.dsk`. MMU по-прежнему только kernel unified PAR:
нет PDR protection/W, MMR1/2, hardware fault metadata, abort250/restart,
modes/I-D/CSM/MAP и high DMA. FP11 отложен, FIS сохранён.

## Продолжение

Следующий ограниченный area experiment строить от CP47c `shared-rx`,
сохраняя CP45k как контроль. Сначала убрать превышение **17 LUT / 10 slices**,
затем получить запас для PDR protection и abort/restart. Предельное
попадание в 1280 LUT само по себе не решает задачу полной MMU.
До успешного fit нет образа для аппаратной проверки этой конфигурации.

## Структурная проверка final EDIF

Финальный CP47c EDIF содержит **54 cells / 3504 nets**, включая четыре
двунаправленные сети. `tools/check_edif_drivers_cp46.py` проверил
направления портов и драйверы: **0 конфликтующих направленных драйверов**,
**0 необъяснённых floating inputs**. Семь неподключённых `CCU2D.CIN`
доказанно не влияют на наблюдаемые outputs по INIT0, реальным constant
connections и vendor-модели; CIN не подменяется нулём.

Три изменённых EDIF с second driver, open input и observable CIN отвергнуты.
Исходный EDIF, mutations и audit JSON сохранены в
[архиве CP47](../tb/reports/cp47/). Это структурный аудит: он не доказывает
электрическое отсутствие конфликтов INOUT, полную netlist equivalence или
routed timing. Raw Synplify warnings сохранены без подавления; отображение
BN161 в логе ограничено первыми 100 сообщениями и не задаёт их общее число.

## Воспроизведение

```sh
python3 tools/build_fram_cp47.py
python3 tools/check_fram_cp47.py
python3 tools/check_fram_cp47_units.py
python3 tools/check_fram_cp47_negative.py
python3 tools/check_fram_cp47_system.py --variant shared-rx --suite cpu
python3 tools/check_fram_cp47_system.py --variant shared-rx --suite cpu --vendor --words 4
python3 tools/check_fram_cp47_system.py --variant shared-rx --suite cpu --edges
python3 tools/check_fram_cp47_system.py --variant shared-rx --suite cpu --vendor --edges
python3 tools/check_fram_cp47_system.py --variant shared-rx --suite bus
python3 tools/check_fram_cp47_system.py --variant shared-rx --suite board
```

Для combined повторить шесть system-команд с `--variant combined`.
`tools/checkpoint_fram_cp47.py` требует нового имени gate и `--variant`;
старые gates не перезаписывать. Build inputs CP44/CP45 и обычные vendor
models нужны как prerequisites. `tools/record_cp47.py` проверяет hashes
всех четырёх synthesis gates, formal/unit/system logs, оба cold FB runs,
EDIF и negative controls перед формированием единого manifest. Reporter
также проверяет неизменность прежнего hardware и backing disk images.
[Manifest](verification-cp47.json),
[архив тестов](../tb/reports/cp47/).
