# CP45 — физический bus CP44: 1351 → 1302 LUT

Полный kernel-relocation board уменьшен на **49 LUT и 24 slices** без
новых тактов: **1302 LUT / 359 FF / 7 EBR / 652 slices**. Повторный gate
CP45k подтверждает результат CP45g на финальных input hashes.
**HC1200 всё ещё не вмещает прототип:** превышение составляет 22 LUT и
12 slices, а PDR protection/restart ещё не подключены. PAR/TRACE не
выполнялись после MAP FAIL, Fmax не получен. CP45 не принят в production.

Production CP40h, принятый APR experiment CP43d и физическая плата CP29a
остаются прежними. Изменения находятся в генераторе build-копий физического
bus; native CPU, relocation bridge, APR controller/EBR, firmware и microcode
954/1024×36 не менялись. Периферия RGB/HDSP/keyboard/HG, UART, SD/RK, KW11
и SPI FRAM сохранена в полном top.

## Изменение выбранного варианта

Все низкие адресные условия read-only I/O mux вычисляются отдельно от
общего `cpu_io_page`. UART/MMR0/MMR3/MAINT/KW11/panel/SD/fixed RK сначала
образуют `io_rdata`, затем общий qualifier применяется один раз:

```verilog
wire [15:0] small_rdata = (io_rdata & {16{cpu_io_page}}) |
                        (local_rdata & {16{local_boot_selected}});
assign rdata = apr_selected ? apr_data : firmware_selected ? boot_program_word :
               fram_selected ? fram_rdata : small_rdata;
```

Запись устройств и ACK продолжают использовать полные qualified selectors.
Порядок главного APR/firmware/FRAM mux сохранён. Нулевой ответ при отсутствии
выбранного устройства также сохранён; контракт не ослаблен до ACK-only.

Точные prefix-сравнения заменяют диапазоны bootstrap ROM, UART и двух SD
registers. Bypass private RK ROM сохраняет исходный диапазон
`160000..160477` octal: prefix `VA[15:9]` плюс
`!VA[8] || VA[7:6]==0`. Он не расширен до всего 512-byte окна.
Bootstrap local ROM декодирует только `word_address[6:1]`, поскольку
`local_boot_selected` уже проверяет все старшие разряды. Значения ROM
снаружи этого selector могут отличаться, наблюдаемый bus response прежний.

Полная проверка PA22 до сужения адреса FRAM, NXM, canonical I/O, обе byte
lanes и private physical DMA исключения сохраняются. Все sequential blocks,
ACK/reset, peripheral instances и side-effect conditions неизменны.

## Synthesis

Diamond 3.14.0.75.2, LCMXO2-1200HC-4SG32C, clock constraint 29.56 MHz,
прежние board pins и strategy. External pin delays не заданы.

| Gate | Вариант | LUT4 | FF | EBR | Slices | Результат |
|---|---|---:|---:|---:|---:|---|
| CP44e | Исходный relocation board | 1351 | 359 | 7 | 676 | MAP FAIL |
| [CP45a](../synth/reports/cp45a/result.json) | Общий qualifier I/O read data | 1339 | 360 | 7 | 672 | MAP FAIL |
| [CP45b](../synth/reports/cp45b/result.json) | Parallel masked memory mux | 1365 | 359 | 7 | 686 | MAP FAIL |
| [CP45c](../synth/reports/cp45c/result.json) | Только prefix decode | 1362 | 359 | 7 | 683 | MAP FAIL |
| [CP45d](../synth/reports/cp45d/result.json) | I/O factoring + prefix | 1320 | 359 | 7 | 662 | MAP FAIL |
| [CP45e](../synth/reports/cp45e/result.json) | D + paired APR/firmware read | 1350 | 359 | 7 | 677 | MAP FAIL |
| [CP45f](../synth/reports/cp45f/result.json) | D + I/O/local priority mux | 1372 | 360 | 7 | 688 | MAP FAIL |
| [CP45g](../synth/reports/cp45g/result.json) | D + narrow local bootstrap ROM | 1302 | 359 | 7 | 652 | MAP FAIL |
| [CP45h](../synth/reports/cp45h/result.json) | G + FRAM/firmware/APR priority | 1357 | 359 | 7 | 680 | MAP FAIL |
| [CP45i](../synth/reports/cp45i/result.json) | G + firmware/APR/FRAM priority | 1330 | 359 | 7 | 666 | MAP FAIL |
| [CP45j](../synth/reports/cp45j/result.json) | G + firmware/FRAM/APR priority | 1338 | 359 | 7 | 670 | MAP FAIL |
| [CP45k](../synth/reports/cp45k/result.json) | G, повтор финальных inputs | 1302 | 359 | 7 | 652 | MAP FAIL |

У всех 954 microcode words; FF в A/F отличаются результатом mapping,
дополнительное RTL state не вводилось. Prefix decode сам по себе оказался
дороже CP44e. Экономия получена сочетанием factoring, prefix и narrow ROM;
стоимости отдельных преобразований не складываются независимо.

Hierarchical Synplify primitives: board bus ORCALUT4 583 → 513, PFUMX
42 → 41; engine ORCALUT4 538 → 536, PFUMX 74 → 75. RTL engine не менялся:
изменение окружения влияет на mapping. Это вложенные primitive counts,
не аддитивная стоимость модулей в финальных MAP LUT4.

Рабочий baseline без MMU CP40h — 1159 LUT / 326 FF / 6 EBR / 584 slices /
31.470 MHz. Принятый APR + MMR3 CP43d — 1258 LUT / 351 FF / 7 EBR /
630 slices / 30.866 MHz; в нём CPU translation отсутствует. Ни один из
этих Fmax не переносится на CP45.

## Verification

`tools/check_bus_cp45.py` извлекает настоящие combinational cones из
baseline CP44e и десяти вариантов. PA22, VA16, device data, raw memory
direction, request, все overlay/RK state bits и enable parameters —
независимые inputs, без ограничений достижимости программой.
Сравниваются 50 observable bits: selectors, read data, APR request/index.
Yosys/YoWASP также проверяет внутренние APR decode points: **81 proven,
0 unproven** у каждого варианта. Намеренные RAM alias через PA21 и
расширение private ROM bypass обнаружены тем же proof.

Icarus проверяет выбранные и невыбранные read words с X/Z при известных
address/control/state inputs: **131072 cases на вариант**. Восемь вариантов
проходят, включая выбранный G/K. B/E превращают выбранный Z word памяти
в X при masked OR: бинарный proof проходит, полная 4-state эквивалентность
нет. Оба варианта отвергнуты; counterexamples сохранены, ошибки не скрыты
waiver. Это проверка RTL semantics, не модель metastability или analog glitches.

Интеграционные тесты повторно используют harness CP44 через отдельные
generated drivers и output paths. CP44 sources/manifests/logs не перезаписаны.
Сохранённые PASS lines отдельных harness имеют исторический префикс CP44;
новые manifests содержат фактический CP45 bus и оба driver source hashes.

| Выбранный `narrow-rom` | Результат относительно CP44 |
|---|---|
| CPU portable: 4096 words × 8 pages × 18/22 modes | 67468380 clocks, 590096 PAR reads, 8 control beats — совпали |
| CPU vendor EBR: 4 words/page/mode | 97692 clocks, 848 PAR reads, 8 control beats — совпали |
| CPU portable/vendor edge tests | Wrap/NXM/I/O, MOVB lane/sign, odd vector4, mapped stack/opcode stream — PASS |
| Full board bus | 32 transaction checks, 6291456 PA/bypass combinations — PASS |
| Cold RT-11FB + DIR | Все clocks/counters и UART transcript совпали |

CPU проверяет каждый word верхних 64 КиБ и неизменность snapshot нижнего
банка. Cold FB: **415159611 clocks**, 4311823 retirements, 5675704 reads /
489280 writes, 3980328 FRAM transactions, 300 RK commands, 576 timer edges,
3270 UART bytes, 162 SD reads / 6 RAM-overlay writes. Mapped beats 526239,
upper-FRAM beats 65664, MMR0 writes 4. Новых clocks нет; результат не
пересчитывается в hardware instructions/sec без fitted Fmax.

MMU-enabled private ROM/DMA beats по-прежнему 0. Active-MMU RK transfer,
high DMA и RT-11XM этим тестом не проверены. FIS corpus отдельно не
перезапускался: CPU/ALU/microcode/relocation unchanged, изменённый bus
проверен formal и полными integration runs. FB backing image и
`../lsi11/disks/rt11v5.3/system.dsk` сохранены; disk hashes проверены.
FPGA не программировалась.

## Продолжение и воспроизведение

Сохранить `narrow-rom` как основу следующего area gate. В этом checkpoint
исследованы decode и read mux; следующий объект — control/handshake и
формирование physical request/address между CPU, APR EBR и FRAM transport.
Только новый synthesis покажет цену такого изменения. До границы HC1200
не хватает 22 LUT / 12 slices, и затем нужен запас на protection/restart.
Нельзя принять вариант, занимающий весь чип, как завершённую MMU.

Границы функциональности остаются [как у CP44](relocation-cp44.md): kernel
unified PAR only, без PDR protection/W, MMR1/2, hardware fault/page metadata,
abort250/restart, modes/I-D/CSM/MAP и high DMA. RT-11XM ещё не загружена.

Из корня `uJ11-fpga` с подготовленными CP44 inputs и обычными simulation
dependencies (YoWASP по умолчанию `build/formal/bin/yowasp-yosys`):

```sh
python3 tools/build_bus_cp45.py
python3 tools/check_bus_cp45.py
python3 tools/check_bus_cp45_system.py --variant narrow-rom --suite cpu
python3 tools/check_bus_cp45_system.py --variant narrow-rom --suite cpu --vendor --words 4
python3 tools/check_bus_cp45_system.py --variant narrow-rom --suite cpu --edges
python3 tools/check_bus_cp45_system.py --variant narrow-rom --suite cpu --vendor --edges
python3 tools/check_bus_cp45_system.py --variant narrow-rom --suite bus
python3 tools/check_bus_cp45_system.py --variant narrow-rom --suite board
```

На Linux/Diamond: `tools/checkpoint_bus_cp45.py` с новым уникальным именем
gate и `--variant narrow-rom`. Все A–K содержат собственные source snapshots;
не перезаписывать старые gates. `tools/record_cp45.py` запускается только
на соответствующих этому checkpoint inputs. Он проверяет 11 raw archives,
hashes текущего K, тесты и неизменность hardware inputs CP40h/CP43d/CP44e.
[Manifest](verification-cp45.json), [архив тестов](../tb/reports/cp45/).
