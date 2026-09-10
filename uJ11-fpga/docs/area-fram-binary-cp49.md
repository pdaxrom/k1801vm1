# CP49 — двоичное кодирование и LSB byte skip FRAM

**Локальные проверки завершены; synthesis ещё не выполнен.** Отправка
семи файлов CP49 на сервер отклонена автоматической проверкой: отдельное
подтверждение CP49 payload запрошено, но ещё не получено. Новых измерений
LUT/FF/EBR/Fmax нет; лучшая измеренная основа остаётся CP47c.

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

Поэтому presence атрибута в RTL недостаточно: после synthesis нужно
проверить в SRR извлечение автомата и таблицу фактического кодирования.
Нельзя по числу объявленных RTL bits заключать, сколько FF получилось.

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

## Проверки до resource gate

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

Все эти результаты — RTL verification; экономия LUT/FF, fit и Fmax
устанавливаются отдельно полным HC1200 synthesis. Имеющийся CP47c занимает
1297 LUT / 351 FF / 7 EBR / 650 slices и не помещается. Цифры CP47c нельзя
переносить на новые варианты до измерения.

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

## Следующий шаг и воспроизведение

После подтверждения передачи выполнить четыре full-board HC1200 gates:
baseline, original, split-low, equations. Проверить фактическое кодирование
state в SRR, LUT/FF/EBR/slices, затем PAR/TRACE при успешном MAP. Даже
попадание в чип не заменяет резерв для protection/restart MMU.

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
`record_cp49.py` пока архивирует локальную проверку и требует обновления,
если появились synthesis reports. [Manifest](verification-cp49.json),
[архив тестов](../tb/reports/cp49/).
