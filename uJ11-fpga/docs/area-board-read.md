# CP38 — уменьшение board read mux

Полный production board уменьшен **1222→1188 LUT**, вариант с CP37 APR
lookup — **1265→1228 LUT**. Изменён только выбор возвращаемых данных
в `boards/hc1200/uj11_board_bus.v`. Decoder, state updates, ACK, UART,
таймер, panel, FRAM/SD transport, RK service, CPU и microcode сохранены.
Новых RTL state bits или тактов нет. Плата остаётся CP29a.

## Измеренные варианты

| Revision | Изменение read path | APR | LUT4 | FF | EBR | Slices | TRACE MHz |
|---|---|---|---:|---:|---:|---:|---:|
| CP36f | Предыдущий production | Нет | 1222 | 326 | 6 | 614 | 31.116 |
| CP37e | Предыдущий APR lookup | Да | 1265 | 341 | 7 | 635 | 30.327 |
| [CP38a](../synth/reports/cp38a/result.json) | Prefix comparisons + narrow local ROM | Да | 1312 | 341 | 7 | 661 | — |
| [CP38b](../synth/reports/cp38b/result.json) | То же + отдельный memory mux | Да | 1247 | 342 | 7 | 627 | 31.258 |
| [CP38c](../synth/reports/cp38c/result.json) | Prefix + полный priority mux | Да | 1281 | 341 | 7 | 642 | — |
| [CP38d](../synth/reports/cp38d/result.json) | Исходный decoder + memory mux | Да | 1228 | 341 | 7 | 619 | 32.470 |
| [CP38e](../synth/reports/cp38e/result.json) | То же без APR | Нет | 1188 | 326 | 6 | 597 | 30.943 |
| [CP38f](../synth/reports/cp38f/result.json) | Финальный production RTL | Нет | 1188 | 326 | 6 | 597 | 30.943 |
| [CP38g](../synth/reports/cp38g/result.json) | Финальный RTL + CP37 APR | Да | 1228 | 341 | 7 | 619 | 32.470 |

Diamond 3.14.0.75.2, LCMXO2-1200HC-4SG32C, constraint 29.56 MHz.
A/C превышают 1280 LUT и 640 slices, Fmax для них не получен.
B/D/E/F/G полностью routed, MAP/PAR/TRACE PASS. F/G подтверждают D/E
после переноса выбранной формы в production file.

Остаток production: **92 LUT / 43 slices / 1 EBR**. Остаток с APR:
**52 LUT / 21 slices / 0 EBR**. APR gate по-прежнему исключает CPU CSR,
write arbitration, MMR, translation/PDR checks/abort-restart и PA22/DMA;
это не результат размещения полной MMU. FP11 не возвращён, FIS сохранён.

В production Fmax немного ниже CP36f: 31.116→30.943 MHz. Номинальные
29.56 MHz проходят с запасом, а площадь уменьшается на 34 LUT.
С APR Fmax вырос 30.327→32.470 MHz при экономии 37 LUT. Приоритет площади
HC1200 сохранён; число instruction/memory clocks не изменилось.

Critical path F: microstore EBR→control→EBR, 32.343 ns, 18 levels,
42.1% logic / 57.9% route, slack 1.512 ns. G: EBR→control→EBR,
30.824 ns, 18 levels, 43.5% logic / 56.5% route, slack 3.031 ns.
External pin delays не заданы; TRACE не является измерением платы.

## Выбранная схема

Прежний `rdata` был OR девяти masked 16-bit buses. Теперь UART,
MAINT, KW11, panel, SD, fixed RK и local bootstrap объединены в
`small_rdata`, а полные memory words выбираются двумя mux:

```verilog
assign rdata = firmware_selected ? boot_program_word :
               fram_selected ? fram_rdata : small_rdata;
```

Взаимная исключительность selectors сохраняется, в том числе во время
physical RK copy cycles и bootstrap overlay. При отсутствии выбранного
устройства ответ остаётся нулём. В отличие от допустимого ослабления
контракта до ACK-only, здесь доказано прежнее значение `rdata` всегда.
Ни один decoder или side-effect condition в production не менялся.

Дешёвые на вид prefix comparisons сами по себе дали 1312 LUT и были
отвергнуты. Добавление memory mux уменьшило результат до 1247, а возврат
исходных decoders — до 1228. Поэтому narrow ROM/prefix rewrite в рабочий
RTL не перенесены. Все четыре кандидата функционально эквивалентны.

Hierarchical Synplify ORCALUT4 у full board bus: CP37e 476→CP38d 448;
CPU с дочерними модулями 659→652. В production bus 480→450, CPU 614→610.
Это вложенные primitive counts, не отдельные MAP LUT costs. Mapping
меняется и за пределами отредактированной строки; числа разных уровней
нельзя складывать. Дополнительный mapped FF у отвергнутого B не означает
добавление state в RTL: register-update blocks во всех вариантах прежние.

## Проверки

`tools/check_board_decode.py` извлекает реальные combinational cones из
frozen CP37e и каждого кандидата. Адрес, write/fetch, все state flags,
read data устройств и BOOT/SD/RK enable передаются как независимые inputs.
Никакие адреса или недостижимые комбинации состояния не исключаются.

Yosys 0.68 / YoWASP: **38 equivalence points — 22 selectors и 16 data bits**,
0 unproven для каждого из четырёх вариантов. SAT проверяет наблюдаемые
outputs с полными cones; внутреннее невыбранное значение local ROM может
отличаться. Намеренные расширение UART range и порча ROM word отвергнуты
тем же proof. Это двухзначная проверка, она не моделирует analog glitches.

Для выбранной формы отдельно побайтно проверено, что весь текст до read
mux и после него прежний. Таким образом, state transitions, ACK/reset,
peripheral instances и write conditions не изменились. Board bus regression
на portable и немодифицированной Lattice DP8KC model прошла по 30 beats:
boot/MAINT, byte lanes, KW11, UART/SD side effects, panel, RK EBR/IRQ/DMA/RTI.

Два полных cold RT-11FB + DIR runs сохранили **все** counters своих baselines:

| Run | Clocks | Retirements | Read / write beats | FRAM transactions | Timer edges |
|---|---:|---:|---|---:|---:|
| Production, как CP36f | 354938300 | 3984366 | 5217011 / 423616 | 3390712 | 576 |
| APR, как CP37e | 412130048 | 3987390 | 5222610 / 424452 | 3397976 | 663 |

В обоих: 300 RK commands, 3270 UART wire bytes, 162 SD reads / 6 writes.
UART transcripts побайтно совпали с прежними. SD writes идут в RAM overlay;
backing FB image и пользовательский XM image не изменены. Гостевые FIS/ISA
algorithms и их ROM images не менялись; отдельный полный ISA corpus в CP38
не запускался повторно. Изменённый bus path проверен formal и board tests.

RT-11XM ещё не загружен: CPU/MMR/translation/high-memory DMA отсутствуют.
Проверка FB не подменяет этот будущий gate.
[Input hashes, reports и counters](verification-cp38.json).

## Воспроизведение и продолжение

```
make test-board-bus-area YOSYS=/path/to/yowasp-yosys
make test-board-bus-area-rt11
python3 tools/record_cp38.py
```

На Linux с Diamond после `make board-bus-area`, в свежих build directories:

```
python3 tools/checkpoint_board_decode.py cp38f --variant current
python3 tools/checkpoint_board_decode.py cp38g --variant current --apr
```

A/B/C archives содержат ранние версии генератора/launcher, D/E — следующий
сравниваемый вариант. Использовать соответствующий `source.tgz`, не менять
исторические reports и не запускать старые record scripts на новых inputs.
Full-board tests могли компилировать build copy: audit проверяет её
побайтное совпадение с финальным production bus и synthesis inputs.

Следующий MMU checkpoint — CPU доступ к APR и арбитраж записи/lookup,
с отдельным новым synthesis gate. Оставшиеся 52 LUT — измеренный запас
read-only конфигурации, а не обещание размещения остальных блоков MMU.
Цель VA16/PAR16/PA22 и 18/22-bit modes остаётся прежней.
