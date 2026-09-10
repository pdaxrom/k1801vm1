# CP41 — проверка альтернатив микросеквенсора

**Ни один из четырёх вариантов не принят. Рабочий RTL остаётся CP40.**
Проверка прямых масок и трёхбитного селектора не дала дополнительного
запаса для MMU. Контрольный синтез в том же каталоге/окружении точно
повторил CP40i: **1258 LUT4 / 344 FF / 7 EBR / 631 slices / 30.254 MHz**.
Свободны 22 LUT и 9 slices, EBR свободных нет. Это ещё APR CSR + lookup,
без трансляции, MMR, автоматического W и abort/restart.

Production CP40h остаётся **1159 LUT / 326 FF / 6 EBR / 31.470 MHz**;
его полные исходные synthesis inputs также проверены по хешам.
Плата остаётся CP29a. Microcode: 954 слова в production, 963 с APR.
FP11 не возвращён, FIS и периферия сохранены, `microasm11` не затронут.

## Измерения полного board

Diamond 3.14.0.75.2, LCMXO2-1200HC-4SG32C, clock constraint 29.56 MHz.
Во всех пяти gates включены те же FIS, FRAM/UART/KW11/panel/SD/RK,
CPU APR CSR и lookup, что в CP40i. Меняется только микросеквенсор.

| Gate | Вариант | LUT4 | FF | EBR | Slices | TRACE MHz | Результат |
|---|---|---:|---:|---:|---:|---:|---|
| [CP41a](../synth/reports/cp41a/result.json) | Маски источников адреса | 1322 | 344 | 7 | 663 | — | MAP overflow |
| [CP41b](../synth/reports/cp41b/result.json) | Маски с общим fault/target | 1306 | 344 | 7 | 655 | — | MAP overflow |
| [CP41c](../synth/reports/cp41c/result.json) | Трёхбитный selector | 1267 | 344 | 7 | 635 | 29.387 | Timing FAIL |
| [CP41d](../synth/reports/cp41d/result.json) | Selector с fault-ветками | 1278 | 344 | 7 | 641 | — | MAP overflow |
| [CP41e](../synth/reports/cp41e/result.json) | Неизменённый CP40, контроль | 1258 | 344 | 7 | 631 | 30.254 | PASS |

A/B/D остановлены на MAP, поэтому Fmax для них отсутствует. D укладывается
в номинальные 1280 LUT, но превышает 640 slices. C полностью размещён и
разведён; Diamond вернул 0, однако TRACE имеет отрицательный slack и
29.387 < 29.56 MHz. Скрипт gate правильно помечает его как неуспешный.
External pin delays не заданы; TRACE не является аппаратным измерением.

Иерархический `design.areasrr` показывает рост самого микросеквенсора:
ORCALUT4 **124 → 162 / 159 / 128 / 133** для A/B/C/D. Эти числа до MAP
не равны полной стоимости модуля: отдельно учитываются CCU2D, PFUMX и
влияние на полную сборку. FF у микросеквенсора остаются 21 во всех gates.
Экономия в CP40 datapath не означает, что аналогичная перестройка другого
mux даст выигрыш. Эти четыре измерения также не доказывают оптимальность
исходного микросеквенсора среди всех возможных архитектур.

## Сопоставление с предыдущим опытом

[CP23](area-sequencer.md) уже отверг независимые маски источников:
FRAM scope вырос с 1098 до 1154 LUT. Тогда был принят общий target с
low-bit OR-dispatch — именно он остаётся в CP40. CP41 повторно измерил
маски в нынешнем полном APR/peripheral scope и подтвердил тот же вывод;
трёхбитный selector с двумя вариантами fault routing также не выиграл.
Цифры CP23 и CP41 относятся к разным полным схемам, их разность нельзя
считать стоимостью одной модификации микросеквенсора. Архив CP23 сохранён.

## Контракт и варианты

Исходник заморожен из source archive CP40i, а не взят из одновременно
редактируемой реализации. Сохраняются интерфейс, все кодировки `uword`,
динамические OR_MS/MD/RR/BT/R67, последовательный uPC, page jump,
conditional branch, dispatch, WAIT/STOP, CALL/RETURN и CALL overflow.
Link и link_valid обновляются на прежних фронтах с прежним enable.

Приоритет next address по-прежнему: reset, затем экспериментальный
context redirect, fault repair, fault redirect, обычное sequencing.
IRQ/trace сохраняют исходный приоритет trace. Проверки не ограничены
словами, которые встречаются в нынешнем микрокоде.

- `flat`: раздельные взаимоисключающие признаки источников и masked OR;
  прежние fault/reset overrides поверх результата.
- `merged`: тот же выбор, с общей веткой target для обычного jump и fault.
- `encoded`: декодирование источника в три бита и сбалансированное дерево
  выбора 10-битных данных; fault overrides остаются снаружи.
- `folded`: fault overrides выбирают тот же трёхбитный selector; подавляется
  OR-dispatch при fault target, STOP и vector015 используют общий вход.

Варианты генерируются только в `build/cp41-seq/`. Файлы `rtl/`, `boards/`,
firmware и рабочие microcode images не изменены.

## Проверки

- Sequential SAT equivalence (Yosys 0.68): четыре варианта × production/APR,
  восемь успешных доказательств. Все controls/data, flags, faults, IRQ/trace,
  reset/enable и совпадающие произвольные uPC/link/link_valid не ограничены.
- Три намеренных дефекта отвергнуты: неверный CALL link, потерянный приоритет
  fault repair и потерянный приоритет context redirect.
- Icarus: **131072 cycles / 262145 comparisons** для каждого из восьми
  вариантов. Проверяются next address и всё состояние до/после фронта,
  все 65536 комбинаций control/command/младших 11 бит uword, псевдослучайные
  остальные входы, reset, holds и подстановка совпадающих состояний.
  Управляющие входы известны; поведение при X на control не заявляется.
- Strict Verilator `--Wall`: восемь конфигураций, без предупреждений и waivers.
- Хеши всех аппаратных входов контрольного gate совпадают с CP40i;
  MAP/PAR/TRACE повторяет его цифры. Полный input manifest production
  CP40h тоже совпадает с текущими файлами.

Новые CPU/FIS и cold RT-11FB прогоны не выполнялись: ни один вариант не
принят, полные исходники ранее проверенной сборки CP40 не изменились.
Предыдущие результаты остаются в [CP40](area-datapath.md), не переименованы
в новые. Образы RT-11FB и RT-11XM проверены по SHA256 и не изменены.
RT-11XM не загружен, CPU по-прежнему не использует верхние 64 КиБ FRAM.

[Verification manifest](verification-cp41.json) связывает исходники,
все пять raw synthesis archives и новые formal/simulation/lint логи.

## Воспроизведение

Из корня `uJ11-fpga`, с Icarus, Verilator, Yosys и существующими generated
microcode/CP39 APR build inputs:

```sh
python3 tools/build_seq_mux.py
python3 tools/check_seq_mux.py --yosys /path/to/yosys
python3 tools/check_seq_mux_sim.py
python3 tools/checkpoint_seq_mux.py cp41a --variant flat
python3 tools/checkpoint_seq_mux.py cp41b --variant merged
python3 tools/checkpoint_seq_mux.py cp41c --variant encoded
python3 tools/checkpoint_seq_mux.py cp41d --variant folded
python3 tools/checkpoint_seq_mux.py cp41e --variant current
```

A–D намеренно возвращают ошибку gate; запускать каждый отдельно и сохранять
отчёты. Для точного исторического повторения брать `source.tgz` данного
gate: ранние archives содержат соответствующие версии генератора. Нужны
свежие implementation directories; существующие отчёты не перезаписывать.
`archive_synthesis.py` проверяет хеши перед архивированием;
`record_cp41.py` связывает результаты с manifest после проверок.

Следующий участок для отдельного измерения — формирование входа D в engine
(константы, MDR, displacement, literal, PSW) и его соединение с APR input.
Сначала нужен измеренный выигрыш полной сборки. Добавление translation/MMR
при нынешних 22 свободных LUT по-прежнему не имеет подтверждённого fit.
