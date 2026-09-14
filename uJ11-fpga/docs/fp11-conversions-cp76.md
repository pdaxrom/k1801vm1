# CP76: преобразования FP11-A в HALT FRAM

2026-09-14. Добавлены последние шесть семейств FP11: **STEXP, LDEXP,
STCfi, LDCif, STCff, LDCff**. Вместе с CP75 реализованы **46 мнемоник /
3949 корректных кодировок** документированного набора. 88 комбинаций с
AC6/AC7 отвергаются; ещё 59 зарезервированных управляющих кодов проверяются
отдельно. Полные внешние DEC diagnostics ещё не пройдены.
[Пакет RT-11](../demos/rt11/service/cp76/README.md).

Это обычный PDP-11 код в верхнем банке FRAM. Hardware CP67b, FIS,
микрокод, FRAM/SD controllers, MMU-less профиль и bootstrap ROM прежние.
**FP11 не установлен на физическую плату.** Ниже приведены результаты
симуляции; для установки файла FPGA перепрошивать не требуется.

## Семантика и документация

| Семейство | Мнемоники | Operand mode 0 | Флаги |
|---|---|---|---|
| STEXP `175000` | STEXP | CPU R0–R7, destination | CPU и FP NZVC |
| STCfi `175400` | STCFI, STCDI, STCFL, STCDL | CPU R0–R7, destination | CPU и FP NZVC |
| STCff `176000` | STCFD, STCDF | FP AC0–AC5, destination | FP NZVC |
| LDEXP `176400` | LDEXP | CPU R0–R7, source | FP NZVC |
| LDCif `177000` | LDCIF, LDCID, LDCLF, LDCLD | CPU R0–R7, source | FP NZVC |
| LDCff `177400` | LDCDF, LDCFD | FP AC0–AC5, source | FP NZVC |

Таблица показывает базовые восьмеричные opcode; AC0–AC3 добавляется в
битах 7:6, operand specifier — в 5:0. Мнемоники выбираются FD/FL, кодировка
внутри семейства общая. У cross-conversion memory operand имеет точность,
противоположную FD. F→D дополняется нулями; D→F усекается при FT=1 и
округляется при FT=0. F-запись в AC сохраняет его младшую половину.

LDCif переводит знаковое целое I/L в F/D. LDCLF допускает потерю младших
битов и использует FT. Для LONG register/immediate берётся один word как
старшая половина, младшие 16 бит — нули. Другие memory modes читают оба
слова, старшее первым. [DEC Architecture Handbook 1983, с. 144](https://www.bitsavers.org/pdf/dec/pdp11/handbooks/EB-23657-18_PDP-11_Architecture_Handbook_1983.pdf).

STCfi всегда отбрасывает дробь к нулю, независимо от FT. Диапазоны
результата — −32768…32767 и −2147483648…2147483647. Выход за диапазон
даёт ноль и C/FC=1; FIC разрешает исключение с FEC=6. Дробь с модулем
меньше единицы становится нулём без conversion error. LONG register/
immediate сохраняет только старшее слово; NZVC вычисляются по всему
32-битному результату. FIUV к исходному AC не применяется.
[DEC PDP-11/70 Handbook 1977–78, с. 8-30](https://bitsavers.org/pdf/dec/pdp11/1170/PDP-11_70_Handbook_1977-78.pdf).

STEXP возвращает exponent−128, в том числе −128 для нулевого exponent.
LDEXP сохраняет sign/fraction и заменяет exponent на (source+128) mod256.
Допустимый signed source — −127…127; −128 уже underflow. При разрешённых
FIV/FIU результат имеет циклический exponent, иначе AC становится точным
нулём. FV отмечает только overflow. Это профиль FP11-A; FP11-C на
некоторых границах отличается.
[DEC Processor Handbook 1981, с. 327–328](https://www.bitsavers.org/pdf/dec/pdp11/handbooks/EB-19402-20_PDP-11_Processor_Handbook_1981.pdf).

Cross-conversion канонизирует exponent-zero в точный ноль. LDC при
выборке отрицательного нуля выставляет FN/FZ, включая FIUV: исключение
не меняет AC, но отражает результат выборки во флагах. Это же правило
исправлено для прежних **LDF/LDD memory FIUV**. D→F rounding overflow
выставляет FV: при FIV=0 результат нулевой, при FIV=1 exponent циклический
и записывается FEC=010. Underflow у смены точности отсутствует.
[DEC Architecture Handbook 1983, с. 142–143, 146](https://www.bitsavers.org/pdf/dec/pdp11/handbooks/EB-23657-18_PDP-11_Architecture_Handbook_1983.pdf).

Проверялся также локальный FP11-A User's Manual 1978 (§5.3.11–14,
SHA256 `be6446903afc3d6ca0e1b0efed85301853979e3a1c5631c8a6e707165c63ecf6`).
Его раннее описание STCfi неоднозначно для |x|<1; принята поздняя
документированная семантика целой части, указанная выше. Раннее описание
LDCLF словами говорит об усечении, поздние таблицы явно задают FT.
[FP11-A manual](https://www.bitsavers.org/www.computer.museum.uq.edu.au/pdf/EK-FP11A-UG-001%20FP11-A%20Floating%20Point%20User%27s%20Manual.pdf).

## Память и исполнение

Общий EA-код выбирает длину 2/4/8 байт. Новое слово CLEN сохраняет
фактическую длину operand, пока общий UNPACK использует R3. FLEN задаёт
размер floating result. Integer-to-float использует прежние AMAN,
ANORM/AROUND/APACK; float-to-integer — сдвиги AMAN и проверку signed range.
Аппаратных сдвигателей, умножителей или нового состояния CPU нет.

Все source words читаются до изменения результата. При abort сохраняются
FP state и прежние CPU flags; уже выполненные memory stores не отменяются.
R0–R6 EA update откладывается до успешного завершения, PC update — по
существующему DCJ11 контракту. FIC и rounding overflow сообщаются после
успешных stores. Поэтому FP trap/IRQ frame у STCfi/STEXP содержит новые
CPU NZVC. Вход ODT откладывается до конца всей инструкции. Ошибка записи
самого trap frame оставляет процессор в терминальном HALT.

FP11.MAC **00.09**, absolute HALT module ABI3 / format2:
**4282 байта кода / 2141 слово**, **268 байт BSS/стека**, всего **4550 байт**.
Прирост от CP75 — 1128 байт кода и 2 байта состояния. FP11.BIN занимает
**5120 байт / 10 блоков RT-11**: header и девять data blocks.
Checksum `105731` (35801) проверяет immutable code; BSS в неё не входит.

| Область | Восьмеричные адреса |
|---|---|
| Код | 040000–050271 |
| FPS / FEC / FEA | 050272 / 050274 / 050276 |
| AC0–AC5 | 050300–050357 |
| Saved registers/PSW | 050360–050401 |
| AMAN / BMAN / PMAN | 050442 / 050452 / 050462 |
| Стек 128 байт | 050506–050705 |
| MEMEND / свободно до 160000 | 050706 / 36410 байт |

Полный диапазон до MEMEND зарезервирован вручную. Автоматического BSS
allocator и relocation нет. Cold init очищает всё состояние и публикует
FP-ready последним; ODT/SDBOOT работают в любом проверенном порядке.

## Эталон и проверки

Общий `core/` не изменён. Private copy сохраняет CPU/EA и адаптации
CP73/74. MOD helpers побайтно прежние. Для новых stores отдельно исправлен
порядок записи флагов/исключений: после memory accesses. В оригинальном
STCfi отсутствовал `SET_C(c_flag)`; private copy задаёт вычисленный carry.
Числовое округление STCff выполняется на копии контекста, чтобы не
запланировать FP trap до возможного bus abort. LDC сохраняет FN от
выборки отрицательного нуля. Эти адаптации перечислены в исходнике
`tools/fp_reference_cp76.py` и архивируются вместе с original SHA256.

Field128 явно отделяет ручные ожидания от differential cases:
1 — invalid AC; 2 — immediate ReadFP abort; 3 — unary write abort;
4 — TST-before-UV; **5 — LDF/LDC flags при memory FIUV**.
Из 225084 fixtures CP75 **223324 полностью сохранены**, а в 1760
изменены только FPS FN/FZ/V/C и marker5 по правилу DEC. Остальные поля,
включая номер случая, не изменились. Добавлены **219360** новых fixtures.

Полная матрица: **444444** случая, 4037 кодировок, 5632 floating opcode/FD
сочетания, 6144 conversion opcode/FD/FL сочетания; **33256** operand fault
injections и **7524** odd probes. 436396 ожиданий reference и 8048 ручных:
704/90/3166/440/3648 по классам 1–5. Boundary tests проверяют 32 набора
всех восьми FPS controls, register и memory operands. STC в SP проверяется
под FID: conversion error обнуляет SP, и последующий frame попал бы в
I/O page. Терминальные stack faults проверяются отдельным harness.

Независимая Python Fraction модель сравнена с private C conversion engine:
**251328 сравнений PASS**, включая все 65536 exponent operands и все
65536 коротких целых. Применяются шесть семейств, F/D, I/L, FT,
FIV/FIU/FIC, отрицательные и ненормализованные нули. Это проверка числового
эталона; исполнение самого firmware отдельно проверяется на RTL.

| Проверка | Случаи | Проверки | Результат |
|---|---:|---:|---|
| Sync ROM, полный набор | 444444 | 30951840 | PASS |
| Logic decode, все кодировки/faults | 215784 | 15052576 | PASS |
| Vendor DP8KC, выборка | 1092 | 76360 | PASS |
| Directed sync | 111 | 4174 | PASS |
| Directed vendor | 111 | 4174 | PASS |
| SPI FRAM, modules/benchmarks | 5 | 1144 | PASS |

Logic сохраняет все кодировки, fault injections и odd probes; 6014 ручных
строк (704/90/3112/220/1888). Vendor DP8KC — выборка 60 кодировок,
256 injections, без odd probes, 80 ручных строк (12/12/56/0/0);
полный набор на vendor models не повторяется. Выборка vendor исполнена
параллельными процессами Icarus: части по 128 случаев, четыре процесса.
Границы кратны восьми, поэтому ack delays сохраняют исходную фазу.
Проверяется точное восстановление всей выборки, PASS каждого фрагмента
и сумма checks/manual; исходный генератор и testbench общие с последовательным
режимом. Журналы и vectors всех частей включены в архив.

Первый RT-11 прогон остановился на ошибке ожидаемой константы: `c780`
кодирует −16384, а для −32768 требуется `c800`. Проверка исправлена;
точной Fraction-моделью также уточнены signed boundary fixtures. Машинный
код firmware не изменился (SHA256 сопоставлен с неуспешным прогоном).
Журнал сохранён в `rejected/rt11-constant`. Дополнительный RTL сценарий
исполняет все 14 native conversion instructions подряд без cold reset.

Directed tests покрывают все 59 reserved control encodings, обе ошибки
записи STCDL, приоритет bus abort над FIC/rounding overflow, частичную
memory запись, новый CPU carry в exception frame, IRQ после STEXP,
ODT во время LDCLD и terminal trap-frame fault. Старые проверки MOD,
MUL/DIV, trace, ODT и UV сохранены.

RT-11/UJMOD/ODT: **94 checks / 811627733 clocks / 9429 UART bytes — PASS**.

Native FPTST 00.08 содержит **43 FP STEP**, все шесть conversion families,
LONG immediate/MSW-only store, minimum I/L, LDCLF rounding, FIC/C и
усечение 0.5. Сохранены проверки MOD, D 1/3, divide-zero, возврат RT-11,
cold init и OFF. Только recovery window интеграционных тестов сокращено;
рабочий bootstrap не менялся. Входные дисковые образы не записывались.

[Замороженные исходники, native listings и результаты](../tb/reports/cp76/archive.json).

## Измерения SPI FRAM

USER opcode request → возврат START, nominal clock 29,56 MHz.

| Команда | Clocks | CPU memory beats | ms |
|---|---:|---:|---:|
| STEXP AC2,R5 | 24161 | 381 | 0,817 |
| STCFI AC2,R5 | 121891 | 1960 | 4,124 |
| STCDL AC2,R5 | 123613 | 1987 | 4,182 |
| STCFD AC2,AC5 | 27316 | 431 | 0,924 |
| STCDF AC2,AC5 | 28253 | 446 | 0,956 |
| LDCLD (R2)+,AC2 | 47512 | 755 | 1,607 |
| LDCDF AC5,AC2 | 27437 | 433 | 0,928 |
| LDCFD AC5,AC2 | 27146 | 428 | 0,918 |

Всего **403 измерения**, 144 новых: **24161–126315 clocks**. Это заданные
различающиеся операнды, не worst-case граница. Для LDCif register R5=0,
у memory operands и LDEXP могут быть другие значения/exception paths;
точные входы задаёт генератор `test_fp_board_cp76.py`.

Прежние 259 samples исполняются с тем же кодом/данными и адресами USER
006000…045721; новый набор — отдельная USER программа после reset.
Обе программы проверяют границы bootstrap и данных. **Пять controls
не замедлились**, остальные **254 получили +384 clocks / 6 beats**
из-за двух дополнительных проверок conversion dispatch. При 29,56 MHz
это +12,99 µs на FP instruction. Вход/выход прежние: **316/177 clocks**.
[Сравнение с CP75](../tb/reports/cp76/comparison.json).

Cold ODT+FP+SDBOOT: **3561244 clocks** в обоих порядках; bad FP checksum:
**3508689**; FP+benchmark initializer: **1125033**. Повторный cold reset
очищает mutable state без изменения checksum.

Все **53 обычных synthesis inputs CP67b совпали по SHA256**. Новый
synthesis не выполнялся: прирост **0 LUT / 0 FF / 0 EBR / 0 microinstructions**.
Сохраняются **1244 LUT / 381 FF / 7 EBR / 628 slices / TRACE 32,032 MHz**,
1005/1024 uwords; свободны 36 LUT, 0 EBR и 19 uwords.

## Воспроизведение и следующий этап

```sh
python3 tools/build_fp11_cp76.py
python3 tools/test_fp_math_cp76.py
python3 tools/test_fp_paths_cp76.py
python3 tools/test_fp_paths_cp76.py --mode logic
python3 tools/test_fp_vendor_parallel_cp76.py
# Последовательный эквивалент: tools/test_fp_paths_cp76.py --mode vendor
python3 tools/test_fp_events_cp76.py
python3 tools/test_fp_events_cp76.py --vendor
python3 tools/test_fp_board_cp76.py
python3 tools/run_fp11_rt11_cp76.py --out build/cp76-rt11-new
python3 tools/compare_fp76.py
python3 tools/compare_vectors_fp76.py
python3 tools/verify_fp76.py --current
```

RT-11 требует свежего каталога. Исторические CP воспроизводятся из их
архивов: рабочий FP11.MAC уже CP76. Далее — внешние FP diagnostics,
FP disassembly и просмотр AC/FPS/FEC/FEA в ODT, установка модуля на плату.
Оптимизация длинного программного сдвига STCfi и общего dispatch теперь
имеет измеренный baseline.
