# CP74: MUL/DIV F/D в программном FP11-A

2026-09-14. Добавлены MULF/MULD и DIVF/DIVD, все восемь addressing modes,
AC0–AC5 как источник и AC0–AC3 как аккумулятор результата. Вместе с CP73
это **30 мнемоник / 2181 корректная кодировка**; 64 сочетания с AC6/7
отвергаются. MOD и преобразования ещё не реализованы.

Модуль исполняет обычные PDP-11 команды из HALT FRAM. Hardware CP67b,
FIS, микрокод, memory controller и ROM bootstrap прежние. Пакет **не
установлен на физическую плату**; перепрошивка FPGA для него не требуется.
[Файлы и инструкция RT-11](../demos/rt11/service/cp74/README.md).

## Семантика DEC

MUL вычисляет AC × FSRC, DIV — AC / FSRC. У DIV источник с нулевым
exponent вызывает FEC=4 и сохраняет AC; проверка предшествует нулевому
делимому. При выключенном индивидуальном разрешении overflow/underflow
результат — точный ноль; при включённом сохраняется результат с циклическим
восьмибитным exponent и формируется исключение. FV отмечает overflow.
[Architecture Handbook 1983, DIV с. 141, MUL с. 150–151](https://www.bitsavers.org/pdf/dec/pdp11/handbooks/EB-23657-18_PDP-11_Architecture_Handbook_1983.pdf).

Округление увеличивает модуль при первом отброшенном бите 1, включая
точную половину; FT выбирает усечение. Для MUL/DIV округлённый ответ
совпадает с округлением бесконечно точного результата: ошибка не больше
1/2 ULP; для усечения — не больше 1 ULP.
[Processor Handbook 1981, с. 317](https://www.bitsavers.org/pdf/dec/pdp11/handbooks/EB-19402-20_PDP-11_Processor_Handbook_1981.pdf).

Принят тот же профиль поздней документации, что в [CP73](fp11-arithmetic-cp73.md),
включая описанное там разночтение ранней таблицы FPS. Memory negative zero
при FIUV отменяет арифметику до изменения AC и имеет приоритет над
проверкой нулевого делителя. AC operands не вызывают FIUV.

Ошибка чтения оставляет FP-состояние нетронутым и вызывает bus-error trap.
Успешно прочитанный операнд коммитит EA autoupdate даже при последующем
FP exception. DIV zero сохраняет NZVC, устанавливает FER/FEC/FEA; FID
подавляет вектор 244, но не регистрацию ошибки. IRQ и запрос ODT ждут
завершения сервиса. F-результат сохраняет младшую половину destination AC.

## Реализация

Используются прежние AMAN/BMAN: четыре 16-битных слова, скрытая единица
в bit 62 и семь guard bits. Добавлен только восьмибайтный PMAN для
произведения/частного. Общие UNPACK, нормализация, округление, APACK,
запись AC/FPS и обработка исключений остаются от ADD/SUB.

MUL складывает мантиссу с накопленным произведением по очередному биту
множителя, затем логически сдвигает накопитель вправо. Это 24 итерации для
F и 56 для D. Сохраняемых guard bits достаточно и после единственного
возможного сдвига нормализации; discarded low product bits не меняют
округлённого результата.

DIV применяет restoring division: сравнение остатка и делителя без знака,
вычитание при возможности, очередной бит частного и сдвиг остатка.
F строит частное в старших двух словах PMAN за 31 итерацию, D — во всех
четырёх за 63. Остаток меньше удвоенного делителя и помещается в 64 бита.
Выход по точному нулю остатка пока не оптимизирован. Частное нормализуется
не более чем одним левым сдвигом, затем используется общая упаковка.

Нет аппаратных multiplier/divider, дополнительных EIS инструкций или
microinstructions. Это первый функциональный baseline программных циклов;
скорость зависит от данных и задержек SPI FRAM.

## Проверка и происхождение эталона

`tools/fp_reference_cp74.py` создаёт отдельную копию CPU/EA из `../core/`.
Общий emulator не меняется, copyright сохраняется. ADD/SUB и unary UV
следуют адаптациям CP73; новые MUL/DIV helpers используют точные
128-битные целые произведение и отношение вместо алгоритмов firmware.
Исходные и адаптированные файлы отдельно отмечены SHA256. Это сравнение
с **адаптированным эталоном**, не с физическим FP11-A.

Python Fraction независимо вычисляет точные значения и округление.
Проверены **200000 сравнений C/Python**, столько же сравнений с моделью
последовательных 64-битных циклов и **155520 точных границ ошибки** для
результатов без exponent exception. Host float/double не используется.

Fixtures включают все FD/FT/FIUV/FIV/FIU/FID combinations, 32 пары границ
MUL/DIV в AC и memory, знаки/dirty zero, 1/3, half-ULP и carry, минимальные
и максимальные exponents, wrap в нулевой exponent, AC self-aliases,
сохранение F low half, все addressing modes, ошибки чтения/записи и odd
addresses. Сохранён предыдущий набор управления, transfers, unary,
compare и ADD/SUB.

Field 128 по-прежнему явно выделяет четыре класса ручных ожиданий:
invalid-AC trap, immediate read abort, unary write abort и TST flags-before-UV.
В CP74 уточнён второй: исходный ReadFP после immediate bus abort продолжает
DIV и ошибочно записывает FEC=4/FEA. Ожидание теперь сохраняет FEC/FEA
вместе с FPS/AC. USER bus-error frame остаётся от CPU reference. Исходный
сбой и fixture сохранены в `rejected/oracle-abort` архива.

| Проверка | Случаи | Проверки | Результат |
|---|---:|---:|---|
| Sync ROM, полный набор | 194396 | 13529088 | PASS |
| Logic decode, все кодировки/faults/границы | 64248 | 4519200 | PASS |
| Vendor DP8KC, целевая выборка | 304 | 21764 | PASS |
| IRQ/trace/ODT/faults, sync | 43 | 1527 | PASS |
| IRQ/trace/ODT/faults, vendor | 43 | 1527 | PASS |
| SPI FRAM, cold modules/benchmarks | 4 | 812 | PASS |

Полный набор содержит 18296 fault injections и 2948 odd-address probes,
4096 floating opcode/FD combinations. Из 194396 ожиданий **190212**
приходят непосредственно от адаптированного reference, **4184** отмечены
отдельно (512/66/3166/440 по классам 1–4). Logic сохраняет все кодировки,
ошибки и odd probes. Vendor содержит 120 injections без odd probes; это выборка 45 кодировок,
86 opcode/FD combinations, а не повторение всего пространства.
`compare_vectors_fp74.py` подтвердил побайтное сохранение всех **149404**
fixtures CP73, кроме изменившегося номера случая; добавлено **44992** новых.

Длинное деление проверено при запросе ODT внутри DLOOP; проверены IRQ
между словами результата MULD, DIV zero с FID/без FID и приоритет FP trap
перед IRQ. В RT-11 проверяются установка через UJMOD, cold init, **22 FP
STEP**, собственная самопроверка FPTST (округлённое D 1/3 и сохранение AC
при DIV zero), возврат в RT-11/DIR, cold init и OFF при сохранённом ODT.

RT-11/UJMOD/ODT: **66 checks / 628973209 clocks / 5523 UART bytes — PASS**.

Все результаты — симуляция. Recovery window сокращено только в
интеграционных тестах; рабочая ROM прежняя. [Архив исходников, результатов,
native listings и UART](../tb/reports/cp74/archive.json).

## Память и ресурсы

FP11.MAC **00.07**, absolute HALT module CP67 ABI3 / format 2:
**2754 байта кода / 1377 слов**, **266 байт состояния и стека**, всего
**3020 байт**. Прирост к CP73 — 510 байт: 502 кода и 8 рабочих данных.
FP11.BIN — 3584 байта / семь блоков RT-11 (один header + шесть data); checksum `061307` (25287).

| Область | Восьмеричный адрес |
|---|---|
| Код | 040000–045301 |
| FPS / FEC / FEA | 045302 / 045304 / 045306 |
| Шесть AC | 045310–045367 |
| Saved registers/PSW | 045370–045411 |
| AMAN / BMAN / PMAN | 045450 / 045460 / 045470 |
| Стек 128 байт | 045514–045713 |
| MEMEND / свободно до 160000 | 045714 / 37940 байт |

Таблица хранит immutable length; **весь диапазон до MEMEND** нужно
резервировать для модуля. Автоматического BSS allocator/relocation нет.
Cold init очищает BSS/stack, проверка checksum охватывает код.
Уточнение к документации CP73: её BIN имеет 3072 байта / шесть блоков
(пять data + один header), а не 2560; FRAM allocation 2510 байт была верна.
Байты старого пакета и замороженный архив не изменены. С ODT и
SDBOOT в обоих порядках: **3282008 clocks**; плохая checksum FP:
**3229829**; FP плюс benchmark: **845797** в модели SPI FRAM.

53 обычных synthesis inputs CP67b совпали по SHA256. Прирост **0 LUT /
0 FF / 0 EBR / 0 uwords**. Новый synthesis не запускался, поскольку
hardware inputs прежние: **1244 LUT / 381 FF / 7 EBR / TRACE 32,032 MHz**,
1005/1024 uwords. Остаток 36 LUT/0 EBR, nominal clock 29,56 MHz.

## Измеренная скорость

USER opcode request → возврат START, полная модель SPI FRAM. AC2/AC5
предварительно содержат одинаковые положительные операнды с ненулевой
дробью; MUL возводит их в квадрат, DIV получает точную единицу.

| Операция AC5,AC2 | clocks | CPU memory beats | Время при 29,56 MHz |
|---|---:|---:|---:|
| MULF | 151755 | 2435 | 5,134 ms |
| MULD | 270036 | 4344 | 9,135 ms |
| DIVF | 153290 | 2449 | 5,186 ms |
| DIVD | 242853 | 3884 | 8,216 ms |

36 новых измерений — **138787–295877 clocks**. Memory/immediate operands
различаются; это конкретные workloads, не worst-case bound. Для новых
memory cases добавлены ненулевые heads по 051770/051774/056000. Вход и
возврат остались 316/177 clocks.

Из прежних **205** измерений **133 совпали**, **72 получили +384 clocks**
(шесть memory beats) из-за dispatch проверок: ADD/SUB и ABS/NEG. Прямые
AC transfers не замедлились. [Сравнение с CP73](../tb/reports/cp74/comparison.json).

Расширенная benchmark-программа сначала пересекла собственные данные
по 050000; этот test fixture был отвергнут. Программа перенесена с 020000
на **010000**, добавлен build-time assert границы ниже return sentinel
046000. Прежние заданные данные и R0–R6 сохранены; абсолютные PC программы
и значение операнда LDFPS R7 отличаются от CP73. Сбой, прежний генератор и memory image
сохранены в `rejected/board-overlap`; исправленный SPI прогон проходит.

## Воспроизведение

```sh
python3 tools/build_fp11_cp74.py
python3 tools/test_fp_math_cp74.py
python3 tools/test_fp_paths_cp74.py --mode sync
python3 tools/test_fp_paths_cp74.py --mode logic
python3 tools/test_fp_paths_cp74.py --mode vendor
python3 tools/test_fp_events_cp74.py
python3 tools/test_fp_events_cp74.py --vendor
python3 tools/test_fp_board_cp74.py
python3 tools/run_fp11_rt11_cp74.py --out build/cp74-fp11/rt11-new
python3 tools/compare_fp74.py
python3 tools/compare_vectors_fp74.py
python3 tools/verify_fp74.py --current
```

RT-11 требует свежего каталога, исходные диски не записываются. Проверка
на плате, полные FP11 diagnostics, MOD, преобразования, FP disassembly и
AC/FPS dump в ODT остаются следующими задачами. Исторические checkpoints
воспроизводятся по их архивам; рабочий FP11.MAC уже относится к CP74.
