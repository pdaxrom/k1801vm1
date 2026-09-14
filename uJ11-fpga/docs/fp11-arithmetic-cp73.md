# CP73: ADD/SUB F/D в программном FP11-A

2026-09-14. Добавлены ADDF/ADDD и SUBF/SUBD, все восемь addressing modes,
AC0–AC5 как источник и AC0–AC3 как аккумулятор результата. С прежними
операциями это **26 мнемоник / 1685 корректных кодировок**; ещё 48 сочетаний
с AC6/7 отвергаются. MUL/DIV, MOD и преобразования ещё не реализованы.

Код выполняется обычным PDP-11 процессором из HALT FRAM. FPGA CP67b,
микрокод, FIS, memory controller и ROM bootstrap прежние. Новый модуль
**не установлен на физическую плату**. [Пакет для RT-11](../demos/rt11/service/cp73/README.md).

## Документированная семантика

DEC описывает семь guard bits у FP11-A/F, три у FP11-C; для округлённого
D-вычитания максимальная ошибка A/F — 33/64 единицы последнего разряда.
Округление увеличивает модуль при первом отброшенном бите 1, включая точную
половину; FT выбирает усечение. [Processor Handbook 1981, с. 317](https://www.bitsavers.org/pdf/dec/pdp11/handbooks/EB-19402-20_PDP-11_Processor_Handbook_1981.pdf).

При overflow/underflow с выключенным индивидуальным разрешением результат
равен точному нулю. При включённом разрешении сохраняется результат с
циклическим восьмибитным exponent, записываются FER/FEC/FEA; FID подавляет
лишь вектор 244. Overflow устанавливает FV. FIUV для ADD/SUB отменяет
операцию до изменения AC. [Architecture Handbook 1983, ADDF/ADDD, с. 137](https://www.bitsavers.org/pdf/dec/pdp11/handbooks/EB-23657-18_PDP-11_Architecture_Handbook_1983.pdf).

Есть разночтение: общая таблица FPS раннего EK-FP11A-UG-001 (1978,
табл. 4-1) описывает overflow при FIV=0 иначе. Здесь принята более поздняя
явная спецификация операций DEC 1981/1983. Исходный PDF 1978 уже использован
в [CP71](fp11-unary-cp71.md); контрольная сумма и происхождение там сохранены.

**Исправление CP71/CP72:** ABS/NEG над memory negative zero при FIUV=1
сначала записывают точный ноль и выставляют FZ, затем вызывают исключение.
Прежняя отмена до записи была ошибочной для выбранного профиля FP11-A.
TST по-прежнему обновляет flags перед исключением.
[DEC 1983, ABS и NEG, с. 136–137 и 152](https://www.bitsavers.org/pdf/dec/pdp11/handbooks/EB-23657-18_PDP-11_Architecture_Handbook_1983.pdf).

## Как устроено вычисление

UNPACK создаёт два 64-битных целых AMAN/BMAN из четырёх 16-битных слов:
скрытая единица — bit 62, семь младших битов — дополнительные. В F режиме
два младших слова исходного AC не участвуют; этот же рабочий формат
позволяет единую нормализацию. Значения с нулевым exponent становятся
положительным точным нулём после проверки memory FIUV.

Сначала выбирается больший модуль. Меньшая мантисса сдвигается вправо на
разницу exponent; разница от 64 отбрасывает её целиком. При одинаковых
знаках выполняется сложение с переносом по четырём словам; при разных —
вычитание с заёмом. SUB предварительно меняет знак ненулевого источника.
Результат нормализуется побитовыми ASL/ROL или CLC/ROR, затем округляется
по FT/FD и упаковывается. F-результат оставляет младшую половину destination
AC нетронутой. Нормализованный результат с exponent=0 отличают от точной
отмены: первый должен сигнализировать underflow, даже если packed bits=0.

Все USER чтения завершаются до изменения AC. Незавершённый доступ сохраняет
AC/FPS. R0–R6 autoupdates коммитятся после удачных operand accesses; PC
обновляется при чтении extension/pointer, как в предыдущих checkpoints.
FP exception после арифметики видит уже записанные AC, FPS и autoupdate.
Ошибка записи внутри trap frame терминальна; это проверяется отдельно от
обычного возврата через bus-error vector. IRQ и запрос пульта ожидают
завершения всего FP service.

## Эталоны и границы проверки

Общий `../core/` не менялся. Его трёхбитный арифметический эталон не подходит
для побитного сравнения FP11-A. `tools/fp_reference_cp73.py` создаёт частную
копию CPU/EA в build и явно заменяет только ADD/SUB arithmetic helper и
порядок FIUV для ABS/NEG. Copyright исходного эмулятора остаётся в копии;
исходные и изменённые файлы имеют отдельные SHA256 в adaptation.json.
Это **адаптированный эталон**, а не доказательство совпадения с неизменённым
J-11 emulator или физическим FP11-A.

Новый `tb/reference_fp_add_cp73.h` использует uint64_t; независимый Python
эталон применяет целые произвольной длины, divmod и Fraction. Проверено
**200000 сравнений и 128104 точных границ ошибки** без host float/double.
Пары, различающие три и семь guard bits, вошли в RTL fixtures. Проверяются
все комбинации FD/FT/FIUV/FIV/FIU/FID, cancellation, переносы между словами,
округление exponent overflow, underflow с packed zero, большой exponent gap,
самоалиасы AC, сохранение F low half, byte-exact USER writes и trap frames.

Четыре прежних класса явно отмеченных ожиданий сохранены в vector field 128:
invalid AC trap, immediate read abort, FPS commit при unary write abort и
FP11-A TST flags-before-UV. Эти случаи учитываются отдельно; число
неотмеченных случаев не объявляется числом сравнений с исходным J-11.
Повторная ошибка trap frame исключена из ordinary operand fault vectors
и проверяется терминальным directed сценарием, включая изменённый SP.

| Проверка | Случаи | Проверки | Результат |
|---|---:|---:|---|
| Sync ROM, полный набор | 149404 | 10426000 | PASS |
| Logic decode, все кодировки/faults/границы | 46040 | 3254816 | PASS |
| Vendor DP8KC, целевая выборка | 472 | 33088 | PASS |
| IRQ/trace/ODT/faults, sync | 39 | 1389 | PASS |
| IRQ/trace/ODT/faults, vendor | 39 | 1389 | PASS |
| SPI FRAM, cold modules/benchmarks | 4 | 732 | PASS |

Полный набор содержит 15064 fault injections и 2244 odd-address probes.
145364 ожидания получены непосредственно из адаптированного reference,
4040 отмечены отдельно (384/50/3166/440 по классам 1–4). Logic сохраняет
все injections и odd probes, vendor — 120 injections, без odd probes.

RT-11/UJMOD/ODT: **56 checks / 592741136 clocks / 4779 UART bytes — PASS**.
Установка FP11, cold init, 18 последовательных FP STEP, самостоятельные
проверки FPTST (включая ABS memory FIUV), возврат в RT-11, DIR, повторный
cold init и OFF при сохранённом ODT. Native MACRO listing дополнительно
проверяется: FP immediate `#1.0` должен дать слово `040200`.

[Архив исходников и сырых результатов](../tb/reports/cp73/archive.json)
содержит vectors, частные reference copies, native assembly и UART log.
Все результаты получены в симуляции. Recovery window сокращено только
в интеграционном тесте тем же документированным способом, что в CP67–72;
рабочие FPGA ROM/constraints не менялись.

## FRAM и FPGA

DEC MACRO/LINK собирают FP11.MAC **00.06** как absolute module CP67 ABI3.
Код — **2252 байта / 1126 слов**, состояние и стек — **258 байт**,
всего **2510 байт** (+886 к CP72: +858 код, +28 рабочие данные).
FP11.BIN — 2560 байт / пять блоков RT-11, checksum `032371` (13561 decimal).

| Область | Восьмеричные адреса |
|---|---|
| Код | 040000–044313 |
| FPS / FEC / FEA | 044314 / 044316 / 044320 |
| Шесть AC | 044322–044401 |
| Saved registers/PSW | 044402–044423 |
| AMAN / BMAN | 044462 / 044472 |
| Стек 128 байт | 044516–044715 |
| MEMEND / свободно до I/O page | 044716 / 38450 байт |

Таблица модулей хранит immutable length; весь диапазон до MEMEND надо
резервировать отдельно. Relocation и автоматического BSS allocator нет.
Cold init очищает состояние без изменения checksum. С ODT/SDBOOT в обоих
порядках получено **3188889 clocks**, с плохой checksum FP — **3138214**,
FP плюс тестовая программа — **752678**. Это clock counts модели SPI FRAM.

53 обычных файла synthesis manifest CP67b проверяются по SHA256.
Прирост **0 LUT / 0 FF / 0 EBR / 0 microinstructions**. Новый synthesis не
запускался: hardware inputs прежние. Baseline CP67b — **1244 LUT / 381 FF /
7 EBR / 32,032 MHz**, 1005/1024 uwords; nominal clock 29,56 MHz.

## Производительность

Полная SPI FRAM модель; интервал — USER opcode request до возврата START.
Результаты для конкретных operands, не worst-case оценка:

| Операция | clocks | CPU memory beats | Время при 29,56 MHz |
|---|---:|---:|---:|
| ADDF AC5,AC2, равные operands | 65229 | 1039 | 2,207 ms |
| ADDD AC5,AC2, равные operands | 67862 | 1080 | 2,296 ms |
| SUBF AC5,AC2, точная отмена | 52887 | 840 | 1,789 ms |
| SUBD AC5,AC2, точная отмена | 54902 | 871 | 1,857 ms |

36 новых измерений: **51744–133144 clocks**. Все они включают программную
распаковку/упаковку; это первый функциональный baseline, не оптимизированная
FP-арифметика. Вход/возврат прежние 316/177 clocks.

Из прежних 169 измерений **63 совпали**, **106 замедлились на 384–576 clocks**
из-за дополнительных проверок в общем FEXEC/FFLAGS. Прямые LDF/STF AC paths
CP72 сохранились. Повторная оптимизация dispatch, сдвигов и упаковки остаётся
в TODO. [Машинное сравнение](../tb/reports/cp73/comparison.json).

## Воспроизведение

```sh
python3 tools/build_fp11_cp73.py
python3 tools/test_fp_math_cp73.py
python3 tools/test_fp_paths_cp73.py --mode sync
python3 tools/test_fp_paths_cp73.py --mode logic
python3 tools/test_fp_paths_cp73.py --mode vendor
python3 tools/test_fp_events_cp73.py
python3 tools/test_fp_events_cp73.py --vendor
python3 tools/test_fp_board_cp73.py
python3 tools/run_fp11_rt11_cp73.py --out build/cp73-fp11/rt11-new
python3 tools/compare_fp73.py
python3 tools/verify_fp73.py --current
```

RT-11 build/run требует свежий каталог. Тесты используют рабочие копии
дисков и не записывают исходный system image. Archive/verifier проверяют
пакет, полные inputs, native listings и сырые логи. Проверка на физической
плате и полные FP11 diagnostics остаются отдельными задачами.
