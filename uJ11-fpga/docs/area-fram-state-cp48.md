# CP48 — явные переходы FRAM и one-hot отклонены по площади

**Оба новых варианта больше CP47c; рабочая экспериментальная основа не меняется.**
Контроль CP48a воспроизвёл **1297 LUT / 351 FF / 7 EBR / 650 slices**.
Явные переходы дали 1319 LUT, one-hot — 1313. Все три full-board gates
завершились MAP FAIL. Лучший CP47c по-прежнему превышает HC1200 на
**17 LUT / 10 slices**, ещё без защиты и restart MMU.

## Задача и варианты

В CP47c SPI FRAM state — четырёхбитный счётчик. После serial byte выполняется
`state + 1`, кроме byte access в DATA_LO, который переходит сразу в DONE.
Проверено, уменьшит ли площадь устранение этого инкремента. Генератор
`tools/build_fram_state_cp48.py` проверяет hash настоящего CP47c transport
и создаёт отдельные файлы в `build/cp48-state/`.

- `baseline`: точная копия CP47c shared-rx.
- `successors`: вместо инкремента перечислены WREN→GAP, CMD→BANK,
  BANK→HIGH, HIGH→LOW, LOW→DATA_LO, DATA_LO→DATA_HI/DONE, DATA_HI→DONE.
- `onehot`: десять состояний кодируются десятью битами; следующий serial
  state получается сдвигом. Остальная логика и значения SPI-команд прежние.

RF/ALU/Q, CPU/microsequencer, firmware, периферия, APR и relocation bridge
не редактировались. Microcode — **954/1024×36 v12**. Приём по-прежнему
использует high `rdata`, как в CP47c. Поэтому сравнение с CP47c требует
равенства **всего rdata каждый такт**, включая busy. Внешний потребитель
по-прежнему должен захватывать результат по ready, как описано в
[контракте CP47](area-fram-cp47.md).

## Измерения

Diamond **3.14.0.75.2**, Synplify **V-2023.09L-2**, target
**LCMXO2-1200HC-4SG32C**, clock constraint **29.56 MHz**. Прежние pins и
strategy; external pin delays не заданы. Это полный board со всей
периферией и partial relocation, не отдельный synthesis FRAM блока.

| Gate | Вариант | LUT4 | FF | EBR | Slices | Fmax | Words | Результат |
|---|---|---:|---:|---:|---:|---:|---:|---|
| [CP48a](../synth/reports/cp48a/result.json) | baseline CP47c | 1297 | 351 | 7 | 650 | — | 954 | MAP FAIL |
| [CP48b](../synth/reports/cp48b/result.json) | successors | 1319 | 357 | 7 | 661 | — | 954 | MAP FAIL |
| [CP48c](../synth/reports/cp48c/result.json) | onehot | 1313 | 357 | 7 | 658 | — | 954 | MAP FAIL |

PAR/TRACE после MAP FAIL не выполнялись; Fmax и аппаратные instructions/sec
не получены. Raw reports, input manifests и source snapshots сохранены;
все synthesis input hashes сверены с текущими файлами. Изменений в
production CP40h, принятом APR CP43d и физической плате CP29a нет.

### Почему явные переходы не помогли

В baseline Synplify сообщает `Found counter ... state[3:0]`.
В `successors` он распознаёт десять состояний и перекодирует их в десять
one-hot bits. В `onehot` extractor перечисляет одиннадцать кодов, включая
нулевой, и сначала создаёт одиннадцатибитное кодирование. Это перечень
extractor-а; RTL proof от reset допускает только десять исходных legal
states. Итоговый FF count обоих вариантов больше baseline на шесть.

Иерархический FRAM cell: ORCALUT4 **73 / 83 / 75**, PFUMX **9 / 0 / 0**
для A/B/C. Это вложенные primitive counts, не аддитивная стоимость в
финальном MAP. Полная сборка выросла на **22 LUT / 11 slices** у B и
**16 LUT / 8 slices** у C. Экономия инкремента не компенсировала стоимость
получившейся схемы. Не переносить one-hot в рабочую реализацию.

## Проверки

`check_fram_state_cp48.py`: четыре успешных SAT temporal-induction proofs,
два варианта × CLK_DIV=1/3. Сравниваются все outputs, включая SPI pins,
ready/error/busy и rdata, а также соответствующие TX/counters/seen/active.
State сравнивается через canonical-код. Вместе с равенством доказываются
legal state и active только в serial states. Это утверждения proof,
а не assumptions, ограничивающие входы. После первого reset все входы,
включая последующие reset, произвольны. Стабильность request/address/data
на время передачи не использовалась как assumption.

`successors` иначе обрабатывает искусственно введённые illegal states;
равенство заявляется для состояний, достижимых после reset. Инъекции сбоев
в регистр state не входят в этот контракт. Первоначальной индукции без
инварианта active/serial не хватало; окончательный proof включает этот
инвариант и проходит. Formal-only canonical/debug ports не входят в
синтезируемый RTL.

Две намеренные ошибки переходов отвергнуты SAT. Отдельный executable
SPI miter также обнаружил обе ошибки, а не только невозможность proof.

**12 unit runs**: два варианта × CLK_DIV=1/2/3 × miter/memory scoreboard.
На каждый вариант:

| CLK_DIV | Miter beats | Reset offsets | Cycle comparisons | Memory transactions |
|---:|---:|---:|---:|---:|
| 1 | 128 | 128 | 20561 | 2048 |
| 2 | 128 | 256 | 55441 | 2048 |
| 3 | 128 | 384 | 106705 | 2048 |

Итого **365414** сравнения тактов, **1536** reset offsets и **12288**
memory transactions. Miter включает X/Z на payload/MISO при известных
control/address. Независимая модель сравнивает все **128 КиБ**, оба банка,
byte/word/odd и held request без повторного ACK. Reset sweep прерывает
транзакции в разных фазах. Strict Verilator lint обоих RTL чистый.

Оба варианта прошли полную интеграцию:

| Проверка | Результат каждого варианта |
|---|---|
| CPU portable, 4096 words × 8 upper pages × 18/22 | 67468380 clocks, 590096 PAR reads, 8 control beats |
| CPU vendor EBR, 4 words/page/mode | 97692 clocks, 848 PAR reads, 8 control beats |
| CPU portable/vendor edge cases | 18-bit wrap, 22-bit NXM, canonical I/O, MOVB lane/sign, odd vector4, high stack/opcode/immediate — PASS |
| Full bus | 32 transaction checks, 6291456 PA/private-bypass combinations — PASS |

CPU записал/прочитал каждое слово верхних 64 КиБ, сохранив snapshot нижнего
банка. Исторические PASS lines имеют префикс CP44/CP47 по имени общего
harness; CP48 manifests содержат реальные входы и generated driver hashes.

Cold **RT-11FB + DIR** выполнен для `successors`: **415159611 clocks**,
4311823 retirements, 5675704 reads / 489280 writes, 3980328 FRAM transactions,
300 RK commands, 576 timer edges, 3270 UART bytes, 162 SD reads / 6 writes
в RAM overlay. Все counts и UART побайтно совпали с CP47c.
Mapped/high FRAM — 526239/65664 beats; MMR0 writes — 4.
После отрицательного area результата второй cold FB run для onehot не
выполнялся; его CPU/vendor/bus проверки приведены выше. FIS corpus отдельно
не повторялся: CPU/ALU/microcode не менялись.

Final EDIF A/B/C: **3504 / 3483 / 3498 nets**. Направленных конфликтующих
драйверов и необъяснённых floating inputs нет. По семь неподключённых CIN
доказанно не влияют на наблюдаемые outputs через прежний audit и vendor
CCU2D model. Исходные EDIF и JSON сохранены. Это не electrical contention
proof для INOUT, полная netlist equivalence или routed timing. Raw warnings
сохранены, включая ограничение вывода BN161 первыми 100 сообщениями.

## Границы и следующий шаг

MMU остаётся kernel unified PAR relocation с PA18/22 и FRAM17. Нет PDR
protection/length/W, hardware fault metadata, MMR1/2, abort250/restart,
modes/I-D/CSM/MAP и high DMA. В cold FB MMU-enabled private ROM/DMA beats
равны нулю; active-MMU RK и RT-11XM не проверены. Оба backing disk images,
включая `../lsi11/disks/rt11v5.3/system.dsk`, проверены по SHA256 и сохранены.
Плата не программировалась; FIS сохранён, FP11 отложен.

Сохранить CP47c. Если продолжать исследование state logic, следующий
эксперимент должен сохранять двоичное кодирование и отдельно измерять
логику переходов: CP48 смешал это изменение с автоматическим one-hot
recoding. Выигрыш такого эксперимента пока не известен. До физического fit
не хватает 17 LUT / 10 slices; затем нужен запас для полной MMU.

## Воспроизведение

```sh
python3 tools/build_fram_state_cp48.py
python3 tools/check_fram_state_cp48.py
python3 tools/check_fram_state_cp48_units.py
python3 tools/check_fram_state_cp48_negative.py
python3 tools/check_fram_state_cp48_system.py --variant successors --suite cpu
python3 tools/check_fram_state_cp48_system.py --variant successors --suite cpu --vendor --words 4
python3 tools/check_fram_state_cp48_system.py --variant successors --suite cpu --edges
python3 tools/check_fram_state_cp48_system.py --variant successors --suite cpu --vendor --edges
python3 tools/check_fram_state_cp48_system.py --variant successors --suite bus
python3 tools/check_fram_state_cp48_system.py --variant successors --suite board
```

Первые пять system-команд повторить с `--variant onehot`. Нужны прежние
CP44/CP45/CP47 build inputs и обычные simulation tools/vendor models.
На Linux/Diamond `checkpoint_fram_state_cp48.py` принимает новое уникальное
имя gate и `--variant baseline|successors|onehot`; старые gates не
перезаписывать. `record_cp48.py` проверяет hashes, raw reports, тесты,
EDIF и неизменность прежних hardware/disk inputs перед архивированием.

[Verification manifest](verification-cp48.json), [архив тестов](../tb/reports/cp48/).
