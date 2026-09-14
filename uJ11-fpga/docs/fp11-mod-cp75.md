# CP75: MODF/MODD в программном FP11-A

2026-09-14. MODF/MODD добавлены к CP74: **32 мнемоники / 2429 корректных
кодировок**, 72 illegal-AC комбинации. Все восемь addressing modes,
AC0–AC5 как источник, AC0–AC3 как аккумулятор результата. Преобразования
FP11-A ещё не реализованы. [Пакет RT-11](../demos/rt11/service/cp75/README.md).

Модуль выполняет обычные PDP-11 команды в HALT FRAM. Hardware CP67b,
FIS, микрокод, контроллеры FRAM/SD и ROM bootstrap не меняются.
**FP11 не установлен на физическую плату.** Все результаты ниже —
симуляция; для загрузки этого пакета перепрошивка FPGA не требуется.

## Семантика

MOD умножает AC на FSRC и разделяет произведение на целую и дробную
части. Для AC0/AC2 целая часть записывается в соседний AC1/AC3; для
нечётного AC она отбрасывается. Дробь всегда возвращается в выбранный AC.
Целая часть усекается; дробь нормализуется и округляется либо усекается
по FT. Округлённая дробь может стать ±1. При |PROD| ≥ 2**L (L=24/56)
дробный результат принудительно нулевой. FN/FZ определяются дробью,
FC сбрасывается, FV отмечает переполнение произведения. Индивидуальные
FIV/FIU управляют сохранением результата с циклическим exponent либо
точного нуля. FIUV для memory FSRC проверяется до исполнения; FID
подавляет вектор 244, но сохраняет FER/FEC/FEA.
[DEC Architecture Handbook 1983, с. 147–149](https://www.bitsavers.org/pdf/dec/pdp11/handbooks/EB-23657-18_PDP-11_Architecture_Handbook_1983.pdf).

FP11-A формирует 48 значащих бит произведения F и 59 разрядов
промежуточного D-произведения. Это ограничение существенно для MODD:
с ростом целой части точность остатка ухудшается. Использование всех
112 разрядов точного произведения меняло бы наблюдаемое поведение.
[FP11-A User's Manual, таблица 5-2 и §5.3.2](https://www.bitsavers.org/www.computer.museum.uq.edu.au/pdf/EK-FP11A-UG-001%20FP11-A%20Floating%20Point%20User%27s%20Manual.pdf).

Нулевой exponent входа обрабатывается как арифметический ноль. Для F
младшие 32 бита обоих затронутых AC сохраняются. Источник полностью
буферизуется до записи целой части, включая alias с соседним AC.
Overflow сообщается и для нечётного AC, хотя целая часть не сохраняется.
Ошибки чтения не меняют FP-состояние; успешные EA autoupdates сохраняются
при FP exception. IRQ/ODT ждут завершения всей команды.

## Реализация и память

Переиспользуется цикл умножения CP74: 24 итерации F, 56 D. Перед
нормализацией MOD обнуляет четыре младших позиции семи guard bits,
получая требуемые 59 разрядов исходного произведения. Затем маска
отделяет целую часть BMAN от дроби AMAN. Целая часть упаковывается без
округления; дробь проходит общие ANORM/AROUND/APACK. Дополнительных
рабочих данных нет: AMAN/BMAN/PMAN уже существовали в CP74.

FP11.MAC **00.08**, absolute HALT module CP67 ABI3 / format 2:
**3154 байта кода / 1577 слов**, **266 байт BSS и стека**, всего
**3420 байт**. Прирост от CP74 — **400 байт кода**.
FP11.BIN — **4096 байт / восемь блоков RT-11**: один header, семь data.
Checksum `126543` (44387) охватывает код, mutable state исключено.

| Область | Восьмеричный адрес |
|---|---|
| Код | 040000–046121 |
| FPS / FEC / FEA | 046122 / 046124 / 046126 |
| AC0–AC5 | 046130–046207 |
| Saved registers/PSW | 046210–046231 |
| AMAN / BMAN / PMAN | 046270 / 046300 / 046310 |
| Стек 128 байт | 046334–046533 |
| MEMEND / свободно до 160000 | 046534 / 37540 байт |

Резервировать нужно весь диапазон до MEMEND, хотя таблица хранит только
immutable length. Автоматического BSS allocator и relocation нет.
Cold init очищает BSS и стек. ODT/SDBOOT совместимы в обоих порядках.

## Проверка

Private oracle сохраняет CPU/EA из общего emulator, адаптации CP73/74 для
ADD/SUB, MUL/DIV и unary UV. **MOD helper не адаптирован**: используется
его исходный алгоритм с тремя guard bits. Общий `core/` не изменялся.
Field 128 сохраняет явные ручные ожидания invalid-AC, immediate abort,
unary write abort и TST-before-UV; они не выдаются за differential results.

Независимые Python Fraction и C unsigned128 модели проверены на
**200000 сравнений**. На тех же входах отдельно проверен неизменённый
MOD helper общего emulator: ещё **200000 сравнений** результатов,
overflow и FEC. 99596 случаев теряют младшие биты произведения;
104660 имеют обе ненулевые части. Проверка третьим алгоритмом выявила
ошибку первоначальных двух математических моделей: они сохраняли дробь
при |PROD| ≥ 2**L. Модели исправлены по случаю 2 DEC; firmware и полный
instruction oracle уже реализовывали эту границу правильно.

Fixtures покрывают 48 направленных пар для чётного/нечётного AC и memory,
знаки, нули, округление/усечение, переполнение, потерю точности MODD,
сохранение F low half, source aliases, все addressing modes и ошибки.
Все **194396** fixtures CP74 сохранены побайтно, кроме номера случая;
добавлено **30688** новых. Полный набор — **225084** случая, включая
19912 fault injections и 3300 odd probes, 4608 floating opcode/FD сочетаний.
220828 ожиданий получены от reference, 4256 помечены отдельно
(576/74/3166/440 по классам 1–4).

| Проверка | Случаи | Проверки | Результат |
|---|---:|---:|---|
| Sync ROM, полный набор | 225084 | 15632368 | PASS |
| Logic decode, все кодировки/faults | 81544 | 5707456 | PASS |
| Vendor DP8KC, выборка | 276 | 19712 | PASS |
| Directed IRQ/ODT/faults, sync | 49 | 1742 | PASS |
| Directed IRQ/ODT/faults, vendor | 49 | 1742 | PASS |
| SPI FRAM, modules/benchmarks | 4 | 848 | PASS |

Logic сохраняет все 2501 проверяемые кодировки (включая invalid AC),
19912 injections и 3300 odd probes. Его 3982 manual rows распределены
576/74/3112/220 по классам 1–4. Vendor — выборка 30 кодировок,
56 opcode/FD combinations, 120 injections, без odd probes; 68 manual
rows (4/8/56/0). Это не повторение всего пространства на vendor models.

RT-11/UJMOD/ODT: **80 checks / 689803088 clocks / 6825 UART bytes — PASS**.
[Замороженный архив исходников, native listings и результатов](../tb/reports/cp75/archive.json).

Directed tests добавляют IRQ между записью целой/дробной частей,
ODT внутри формирования маски при alias FSRC=AC+1 и integer overflow
на нечётном AC с FID и без него. Отдельно проверены F-округление
дроби до +1/−1 и сохранение low halves обеих частей. Native FPTST включает **29 FP STEP**,
MODF/MODD 1.5×1.5, нечётный AC, самостоятельную проверку −2/−0.25,
предыдущие 1/3, divide-zero и возврат в RT-11. Recovery window сокращено
только в интеграционных тестах; рабочий bootstrap прежний.

## Измерения SPI FRAM

USER opcode request → завершение START. AC2 и AC5 заранее содержат
одинаковые положительные значения с ненулевыми младшими словами.

| Операция AC5,AC2 | Core clocks | Memory beats | При 29,56 MHz |
|---|---:|---:|---:|
| MODF | 197367 | 3166 | 6,677 ms |
| MODD | 315491 | 5072 | 10,673 ms |

18 новых измерений: 143758–382241 clocks. Заданные memory/immediate
операнды отличаются; это не оценка худшего случая. Вход/возврат —
316/177 clocks. Из 241 измерения CP74 **133 совпали**; ADD/SUB и ABS/NEG
получили +192 clocks / 3 beats (72 случая), MUL +322 / 5 (18), DIV
+72 / 1 (18) из-за общего dispatch/возврата из умножения. Прямые AC
transfers не замедлились. [Сравнение](../tb/reports/cp75/comparison.json).

Cold init ODT+FP+SDBOOT — 3355008 clocks в обоих порядках, повреждённая
checksum FP — 3302829, FP+benchmark initializer — 918797.
Первый SPI test ошибочно разместил программу по USER 004000, куда
resident копирует bootstrap. Измерения не начались; сбой сохранён в
`rejected/board-bootstrap`. Исправленный тест начинается по **006000**,
заканчивается ниже 046000; обе границы проверяются при сборке. По сравнению
с CP74 абсолютные PC и операнд LDFPS R7 отличаются; остальные заданные
операнды и R0–R6 прежние.

Все 53 обычных synthesis inputs CP67b совпали по SHA256. Прирост
**0 LUT / 0 FF / 0 EBR / 0 microinstructions**. Новый synthesis не запускался:
аппаратные входы прежние — **1244 LUT / 381 FF / 7 EBR / 628 slices**,
TRACE **32,032 MHz**, nominal **29,56 MHz**, microstore **1005/1024**.
Свободны 36 LUT, 0 EBR и 19 microinstructions.

## Воспроизведение

```sh
python3 tools/build_fp11_cp75.py
python3 tools/test_fp_math_cp75.py
python3 tools/test_fp_paths_cp75.py
python3 tools/test_fp_paths_cp75.py --mode logic
python3 tools/test_fp_paths_cp75.py --mode vendor
python3 tools/test_fp_events_cp75.py
python3 tools/test_fp_events_cp75.py --vendor
python3 tools/test_fp_board_cp75.py
python3 tools/run_fp11_rt11_cp75.py --out build/cp75-fp11/rt11-new
python3 tools/compare_fp75.py
python3 tools/compare_vectors_fp75.py
python3 tools/verify_fp75.py --current
```

RT-11 требует свежего каталога. Исходные диски не записываются.
Исторические CP воспроизводить из их архивов: рабочий FP11.MAC уже CP75.
Далее — преобразования FP11-A, полные diagnostics, FP disassembly и
просмотр AC/FPS/FEC/FEA в ODT, установка модуля на физическую плату.
