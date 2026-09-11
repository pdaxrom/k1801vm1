# CP49 — двоичное кодирование и LSB byte skip FRAM

**Все три альтернативы отклонены по площади: 1325 / 1335 / 1310 LUT.**
Контроль CP49a воспроизвёл CP47c: **1297 LUT / 351 FF / 7 EBR / 650 slices**.
Все четыре MAP gates не помещаются в HC1200; PAR/TRACE/Fmax отсутствуют.
Лучшей экспериментальной основой остаётся CP47c, превышение — **17 LUT /
10 slices**, ещё без резерва для protection/restart MMU.

Цель: отделить стоимость логики переходов от автоматического one-hot
recoding, который увеличил площадь в [CP48](area-fram-state-cp48.md).
Исходная схема — неизменённый CP47c shared-rx, весь board с partial
kernel-unified relocation и 954/1024×36 microcode words.

## Документация инструмента

Прочитано установленное **Synplify Pro for Lattice Attribute Reference,
September 2024**, раздел `syn_encoding`, печатные страницы **67–70**.
Руководство находится на synthesis-сервере в
`/home/sash/.local/lscc/diamond/3.14/synpbase/doc/fpga_attribute_reference.pdf`.
SHA256: `acdd9a122f61c27f8591c4d145abffa5ebb2f10c8021510b0ac3471df94cc42c`.

`original` сохраняет заданное кодирование, при этом FSM/reachability
analysis остаётся включённым. Атрибут действует на распознанный FSM
при включённом FSM Compiler. Verilog-синтаксис у регистра:

```verilog
reg [3:0] state /* synthesis syn_encoding="original" */;
```

Применение атрибута проверено по SRR: все десять кодов `0000..1001`
сопоставлены самим себе. Final EDIF содержит четыре state FF. Заголовок
FSM в SRR обозначен как `state[9:0]`, поэтому вывод сделан по таблице
перекодирования и реальным FF, а не по этому заголовку или объявлению RTL.

## Сравниваемые варианты

- `baseline`: точная копия CP47c.
- `original`: явная таблица переходов CP48 successors и документированный
  атрибут сохранения исходных четырёхбитных кодов.
- `split-low`: прежний двоичный инкремент, но byte override применяется
  только к младшему биту следующего состояния.
- `equations`: тот же инкремент через XOR/AND, с тем же LSB override.

DATA_LO=7, DATA_HI=8, DONE=9. Byte transfer меняет переход 7→8 на 7→9:
меняется только bit0. В двух последних вариантах serial-next выражение
равно исходному increment/skip при всех 16 входных кодах и обоих значениях
byte_access. В `original` поведение при искусственном illegal state может
отличаться; общий контракт всех вариантов — последовательность после reset.

Сохраняются WREN/GAP, read/write command, A16 bank, high/low address bytes,
data lanes, CS/SCK/MOSI, ACK/error/busy и число тактов. High rdata по-прежнему
служит RX scratch во время busy, как в CP47c. Сравнение с CP47c требует
равенства всех 16 bits rdata каждый такт, а не только по ready.

Генератор проверяет hashes исходников CP47c и CP48b. Все изменения создаются
в `build/cp49-binary/`; прежние RTL, generators, manifests и reports
не перезаписываются. Не добавляются состояния CPU/MMU, новые инструкции
или изменения периферии. FIS сохраняется, FP11 остаётся отложенным.

## Полный HC1200 synthesis

Diamond **3.14.0.75.2**, Synplify **V-2023.09L-2**, target
**LCMXO2-1200HC-4SG32C**, constraint **29.56 MHz**. Прежние board pins и
strategy, external pin delays не заданы. Включён полный board с partial
relocation, всей периферией и **954 microcode words**.

| Gate | Вариант | LUT4 | FF | EBR | Slices | Fmax | Words | Результат |
|---|---|---:|---:|---:|---:|---:|---:|---|
| [CP49a](../synth/reports/cp49a/result.json) | baseline CP47c | 1297 | 351 | 7 | 650 | — | 954 | MAP FAIL |
| [CP49b](../synth/reports/cp49b/result.json) | original | 1325 | 351 | 7 | 664 | — | 954 | MAP FAIL |
| [CP49c](../synth/reports/cp49c/result.json) | split-low | 1335 | 351 | 7 | 669 | — | 954 | MAP FAIL |
| [CP49d](../synth/reports/cp49d/result.json) | equations | 1310 | 363 | 7 | 657 | — | 954 | MAP FAIL |

Raw reports, input manifests и source snapshots четырёх gates сохранены.
Каждый synthesis input hash сверен с текущим файлом и ранее проверенными
RTL. Локальные тесты повторно не запускались: их входы не изменились.
Частота constraint не является измеренным Fmax. Ни один вариант не принят;
CP47c, production CP40h, APR CP43d и физическая плата CP29a сохранены.

### Кодирование и причина отказа

В baseline Synplify распознаёт четырёхбитный счётчик. `original` сохраняет
четырёхбитные исходные коды и общее число FF, однако требует **+28 LUT /
14 slices**. То есть отказ CP48 нельзя объяснять только one-hot recoding:
явная таблица с двоичным кодированием также не уменьшила полную схему.

`split-low` имеет четыре state FF, но полная сборка выросла на **38 LUT /
19 slices**. `equations` инструмент распознал как FSM с шестнадцатью
кодами и перекодировал их в 16-битный one-hot, что подтверждено SRR и
шестнадцатью state FF в final EDIF. Итог **+13 LUT / +12 FF / +7 slices**.
Перечень extractor-а из 16 кодов не означает достижимость их всех после
reset: RTL proof по-прежнему доказывает инвариант десяти legal states.

Вложенные Synplify primitive counts показывают влияние изменения на mapping
полной схемы, а не только FRAM:

| Иерархия / primitive | A | B | C | D |
|---|---:|---:|---:|---:|
| FRAM ORCALUT4 | 73 | 73 | 72 | 91 |
| FRAM PFUMX | 9 | 8 | 10 | 0 |
| Board bus ORCALUT4, включая дочерние блоки | 505 | 540 | 556 | 544 |
| CPU ORCALUT4, включая дочерние блоки | 589 | 579 | 574 | 564 |

Исходники CPU и остальной board логики не менялись, но их mapped cones
изменились. Эти counts не являются независимыми MAP LUT costs и не
складываются как стоимость изолированных модулей. По одному почти
неизменному FRAM cell нельзя заключать, что полный board стал меньше.

## RTL verification

- Шесть успешных SAT temporal-induction proofs: три варианта × CLK_DIV=1/3.
  После первого reset все входы, включая последующий reset, произвольны.
  Канонический legal state и active только в serial states доказываются
  вместе с совпадением outputs/control, не задаются assumptions.
- Три намеренных ошибки переходов отвергнуты SAT и отдельно обнаружены
  executable SPI miter.
- 18 unit runs: три варианта × CLK_DIV=1/2/3 × miter/memory scoreboard.
  548121 потактное сравнение, 2304 reset offsets, 18432 memory transactions.
  Miter использует X/Z payload/MISO при известных control/address. Модель
  сравнивает все 128 КиБ, обе banks, byte/word/odd и held request.
- Strict Verilator lint всех трёх generated RTL без предупреждений.

Это RTL verification при двухзначных formal inputs и четырёхзначной
симуляции payload/MISO. Проверки не моделируют analog SPI timing и
не заменяют PAR/TRACE или аппаратную проверку.

## Полный CPU и board

Каждый из трёх вариантов прошёл те же интеграционные проверки:

| Проверка | Результат каждого варианта |
|---|---|
| CPU portable, 4096 words × 8 upper pages × 18/22 | 67468380 clocks, 590096 PAR reads, 8 control beats |
| CPU vendor EBR, 4 words/page/mode | 97692 clocks, 848 PAR reads, 8 control beats |
| Portable/vendor edge cases | Wrap/NXM/canonical I/O, MOVB lane/sign, odd vector4, high mapped stack/opcode/immediate — PASS |
| Full bus | 32 transaction checks, 6291456 PA/private-bypass combinations — PASS |

Записано и прочитано каждое слово верхних 64 КиБ FRAM; snapshot нижнего
банка сохранился. Generated drivers имеют отдельные output paths и
manifests; исторические PASS lines сохраняют имя общего CP44/CP47 harness.

Cold **RT-11FB + DIR** выполнен для `split-low`: **415159611 clocks**,
4311823 retirements, 5675704 reads / 489280 writes, 3980328 FRAM transactions,
300 RK commands, 576 timer edges, 3270 UART bytes, 162 SD reads / 6 writes
в RAM overlay. Все counts и UART совпали с CP47c побайтно.
Mapped/high FRAM — 526239/65664 beats, MMR0 writes — 4.
Отдельные cold FB runs для original/equations не выполнялись; их CPU/bus
проверки приведены выше. FIS corpus отдельно не повторялся: CPU/ALU/microcode
не менялись.

MMU-enabled private ROM/DMA beats — 0. Active-MMU RK/high DMA и RT-11XM
не проверены. MMU по-прежнему kernel unified PAR relocation без PDR
protection/length/W, hardware fault metadata, MMR1/2, abort250/restart,
modes/I-D/CSM/MAP. Оба backing disk images, включая
`../lsi11/disks/rt11v5.3/system.dsk`, проверены по SHA256 и не изменены.
Production CP40h, принятый APR CP43d и физическая плата CP29a прежние.

## Final EDIF audit

Проверены **3504 / 3502 / 3454 / 3369 nets** в A/B/C/D. Конфликтующих
направленных драйверов и необъяснённых floating inputs нет; в каждом
netlist семь CIN доказанно не влияют на наблюдаемые outputs через
INIT0, реальные constant connections и прежнюю vendor CCU2D модель.
CIN не подменяется нулём. Actual state FF counts — **4 / 4 / 4 / 16**.
Raw EDIF сохранены с audit JSON и source/report hashes.

Это структурный аудит, не проверка электрических конфликтов INOUT,
полная gate-level equivalence или routed timing. Прежний checker не
изменялся; его negative controls уже проверены в CP46/CP47. Raw warnings
сохранены без подавления; SRR ограничивает вывод BN161 первыми 100
сообщениями, что не задаёт их общее число.

## Следующий шаг и воспроизведение

Сохранить CP47c. CP48/CP49 закрывают проверенные варианты state recoding,
explicit successors и LSB byte skip: ни один не дал выигрыша. Следующую
гипотезу по площади выбирать по полной board логике и проверять на ней;
не переносить проигравший вариант только из-за меньшего RTL-выражения.
Превышение остаётся 17 LUT / 10 slices, затем нужен запас для protection/
restart MMU. Аппаратная проверка этой конфигурации невозможна до fit.

```sh
python3 tools/build_fram_binary_cp49.py
python3 tools/check_fram_binary_cp49.py
python3 tools/check_fram_binary_cp49_units.py
python3 tools/check_fram_binary_cp49_negative.py
python3 tools/check_fram_binary_cp49_system.py --variant split-low --suite cpu
python3 tools/check_fram_binary_cp49_system.py --variant split-low --suite cpu --vendor --words 4
python3 tools/check_fram_binary_cp49_system.py --variant split-low --suite cpu --edges
python3 tools/check_fram_binary_cp49_system.py --variant split-low --suite cpu --vendor --edges
python3 tools/check_fram_binary_cp49_system.py --variant split-low --suite bus
python3 tools/check_fram_binary_cp49_system.py --variant split-low --suite board
```

Первые пять system-команд повторить с `--variant original` и `--variant equations`.
Нужны CP44/CP45/CP47/CP48 build inputs, обычные tools и vendor models.
Linux/Diamond driver `checkpoint_fram_binary_cp49.py` требует новое
уникальное имя gate и `--variant`; старые gates не перезаписывать.
`record_cp49.py` проверяет четыре synthesis archives, соответствие текущих
inputs, таблицу original encoding, actual state FF, final EDIF audit и
прежние formal/unit/system logs перед формированием общего manifest.
[Manifest](verification-cp49.json),
[архив тестов](../tb/reports/cp49/).
