# CP25: микрокодный MUL

**CP25 завершён: synthesis и полная portable/vendor регрессия прошли.**

## Семантика и источники

MUL `070RSS` умножает два знаковых 16-bit операнда. Сначала читается S,
включая addressing side effects, затем R. Результат записывается старшим
словом в R и младшим в R|1. При нечётном R остаётся только младшее слово;
N/Z описывают полный 32-bit результат, V=0. C показывает, что результат
не помещается в signed 16 bits. Byte MUL не существует.

Проверены локальный [DEC J-11 User Guide](../../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf),
печатная стр. 6-41 (PDF 141), таблица C-1 на C-6/C-7,
и [SIMH PDP11 CPU, case MUL](https://github.com/simh/simh/blob/master/PDP11/pdp11_cpu.c).
SIMH читает S до R, записывает high затем low и вычисляет N/Z по полному
произведению. Общая таблица J-11 помогает проверить addressing ordering;
конкретная последовательность MUL дополнительно сверена с SIMH и AM4.

В предварительном J-11 manual неудачно указана верхняя граница C через
`>= 2^15-1`. Это противоречит проверке представимости signed word:
**+32767 и −32768 имеют C=0, +32768 и −32769 — C=1**.
Выбран критерий `high != sign_extend(low[15])`. Его подробно объясняет
[DEC KE11-E/F Options Manual, §4.7.3.3, стр. 4-32](https://bitsavers.org/pdf/dec/pdp11/1140/DEC-11-HKEFA-A-D_KE11-E_and_KE11-F_Instruction_Set_Options_Manual_197303.pdf#page=72);
он также совпадает с проверкой диапазона в SIMH. Это явное разрешение
противоречия в предварительной документации, а не перенос опечатки в RTL.

Изучен [AM4 mc.asm](../../lsi11-fpga/ucode/experimental/am4/mc.asm):
2b0 вызывает operand fetch через OR_MD, 2b1 читает регистр после EA;
2ba/2bb/2bf организуют цикл, 2b8 — signed correction, 2c3 и 1cc–1cf/2b2–2b7
завершают flags/writeback. AM4 сочетает ALU с RAMQD/ASHXR в одном такте.
В uJ11 отсутствует такой специальный правый сдвиг; буквальное копирование
этого цикла потребовало бы усложнить datapath.

## Реализация

Добавлены **45 слов и 10 меток**: **642/1024×36**, 62.6953125% occupancy,
**255 labels**, encoding v11. Все 597 слов и 245 меток CP24 сохранены.
Единственный изменённый RTL-модуль — decoder; RF 16×16, Q, ALU,
microsequencer, PSW, memory interface и FRAM transport не меняются.

| Адреса, hex | Назначение | Слов |
|---|---|---:|
| 062–063 | S из регистра | 2 |
| 066–067, 082–083 | прежний DESTINATION_EA, read-only word READ, MDR → T2 | 4 |
| 312–31b | позднее R, начальные значения | 10 |
| 333–33d | 16 итераций и signed correction | 11 |
| 373–37e | C, N и полное Z | 12 |
| 395–39a | NZVC и два register writes | 6 |

Decoder распознаёт `IR[15:9]==070` (octal) и выбирает 062/066 по
`|IR[5:3]`. Все восемь S modes используют существующий EA microcode,
который исторически назван DESTINATION_EA. Дополнительного EA engine нет.

Обозначения: M — signed S operand (T2); U — unsigned bit pattern позднего R.
T4:Q начинает с нуля. Каждая из 16 итераций сдвигает T4:Q влево на один
через RFQ_L и, если очередной старший бит U равен 1, добавляет sign-extended
M через ADD Q и ADC high. Затем, если исходный R отрицателен,
из high вычитается M. Это вычисляет
`M*unsigned(R) − (R<0 ? M*65536 : 0)` modulo 2^32.
Нет аппаратного multiplier, barrel shifter, нового FF или нового ALU mux.
Счётчик 16 итераций занимает T1; исходный R/sign — T0/T3; sign extension — T5.

Flags сначала вычисляются в temporaries: C из сравнения high со знаком low,
N из high, Z из OR(high, low), V=0. Финальный LOAD сохраняет прежние IPL/T.
High записывается раньше low, поэтому нечётное R корректно получает low,
но flags остаются от полного результата. Operand fault происходит до
изменения PSW или результата; EA side effects и trap frame проверяются отдельно.

## Исправление существующего C-эталона

В прежнем `core/core.c`, case 0070, R снимался до EA, а после fault при
финальном operand READ выполнение продолжалось и могло испортить trap frame.
[Узкий patch](cp25-core-fix.patch) только для `model==DCJ11` переносит чтение R
после S и возвращает управление при `fAbort`. Остальные CPU models и
инструкции сохранены; fixture expected values вручную не исправлялись.

`tools/check_mul_core_negative.py` воспроизводит ошибки на **архивном CP24 C**:
независимая модель отвергает late-register ordering; **2368 из 4000** fault
случаев меняли регистры или PSW после vector frame. Дополнительных bus beats
после frame было 0. После patch таких продолжений **0**. Исходные negative
fixtures/logs и оба SHA256 сохраняются в CP25 evidence.

## Проверки

Новые RAM и FRAM проверки прошли: **13110 normal cases / 13528 candidates**,
**418 exclusions** (abort 338, vector 144, I/O 56, stack 234; причины могут
пересекаться). Отдельно **4000 fault frames: 3008 ACK errors + 992 odd address**,
без exclusions. Нормальные тесты покрывают 504 encodings, fault — 448,
объединение — все **512 MUL encodings**.

Независимая integer/EA модель проверяет **7002 обычных случая**, включая
**385 aliases**, все восемь modes, регистры, PSW и точный bus trace.
T/IRQ frames в этой дополнительной модели не дублируются: они сравниваются
с выполнением реального C-эталона. Serial arithmetic отдельно проверена
для **861968 пар**: 13 крайних M × все 65536 R и 10000 seeded random pairs.

Двенадцать negative controls отвергают: отсутствие opcode, 15 итераций,
unsigned M/R, потерю переноса low→high, неправильные C/N/Z, неправильный
порядок нечётного writeback, раннее чтение R и PSW commit до operand fault.
**1024 CSR cases** проверяют read-clear side effect, отсутствие write,
все начальные NZVC, waits 0…3, ACK read error и odd address.

Полная свежая регрессия прошла: **255243 instruction cases и 45596 fault frames**
на каждом RAM/FRAM × portable/vendor сочетании, **1082 benchmarks/ROM**.
Все **120 result files** совпали между ROM models; все **22 прежних C fixtures,
44 cycle CSV и 68 benchmark JSON** побайтно равны CP24, включая такты,
memory beats, SPI transactions и SPI clocks. Из illegal candidate list удалены
512 MUL encodings, прежние 4160 completed illegal fixtures сохранены.
Проверены 104 synthesis archives и 411 raw report hashes; текущие inputs
совпадают с CP25a/b. Прошли C regression, unit/directed tests, lint и lsi11
peripheral checks. Decoder miter проверил все 65536 encodings: отличаются
ровно 512 MUL encodings. [Manifest](verification-cp25.json),
[счётчики и производные метрики](benchmarks-cp25.json).

## Synthesis HC1200

| Scope | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---:|---:|---:|---|---:|
| [CP25a core + probe](../synth/reports/cp25a/result.json) | 841 | 299 | 4 | 35 MHz PASS | 36.926 MHz |
| [CP25b FRAM/prefetch/IRQ + probe](../synth/reports/cp25b/result.json) | 1089 | 416 | 4 | 29.56 MHz PASS | 30.672 MHz |

Реальные Diamond MAP/PAR/TRACE, тот же HC1200-4SG32C, strategy, probes и
constraints, что CP24. Изменены только decoder, ROM и имена checkpoint.
Core −13 LUT, FRAM scope без изменения LUT; FF/EBR прежние. Изменение fit
не доказывает отдельную универсальную оптимальность decoder placement.

До предпочтительного верхнего предела 1100 остаётся **11 LUT**; до физического
1280 — 191. Полный UART/timer/panel/SD/RK top и external pin timing в fit
не включены. 50 MHz и 900–1000 LUT для FRAM scope пока не достигнуты.
FPGA не программировалась. MMU отсутствует полностью.

## Benchmarks

16 workloads × ideal RAM / legacy FRAM / sequential FRAM / FRAM+prefetch.
Восемь modes используют M=0xfedc и R=0x8123; ещё восемь register workloads
проверяют ноль, ±1, границы signed word и максимальные signed products.
Loop содержит 31 пару `MOV #R,R0; MUL S,R0`, один BR и, для modes 2…5,
восстанавливающий MOV #0x4000,R2. Один loop прогревается, восемь измеряются.
Проверяются все конечные регистры, PSW, PC/SP и неизменность operand memory.

CPI всего loop включает MOV и BR: его нельзя выдавать за latency одного MUL.
Отдельно учитываются 248 MUL retirements и такты между предыдущим retirement
и MUL retirement, включая fetch/operand wait. Demand beats и реальная SPI
активность измеряются отдельно. CPU/SPI номинально 29.56/14.78 MHz;
это функциональная симуляция транспорта MR45V100A, не измерение платы.

Измерения portable и vendor ROM совпали во всех 64 новых workload runs.
CPI ниже — всего цикла; MUL interval включает fetch и memory waits.

| S mode | Ideal RAM loop CPI | Legacy FRAM CPI | Sequential FRAM CPI | FRAM+prefetch CPI | MUL interval, prefetch | MUL/s в полном loop @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 69.904762 | 226.571429 | 126.190476 | 105.031746 | 156.000000 | 138,486 |
| 1 | 72.857143 | 281.190476 | 214.269841 | 211.317460 | 267.000000 | 68,832 |
| 2 | 72.468750 | 280.828125 | 212.828125 | 209.828125 | 268.000000 | 68,237 |
| 3 | 73.437500 | 332.656250 | 264.656250 | 261.656250 | 375.000000 | 54,721 |
| 4 | 72.468750 | 280.828125 | 212.828125 | 209.828125 | 268.000000 | 68,237 |
| 5 | 72.953125 | 332.171875 | 264.171875 | 261.171875 | 374.000000 | 54,823 |
| 6 | 73.841270 | 333.841270 | 233.460317 | 230.507937 | 306.000000 | 63,102 |
| 7 | 74.825397 | 386.492063 | 286.111111 | 283.158730 | 413.000000 | 51,368 |

FRAM+prefetch счётчики за восемь измеряемых loops: всего 248 MUL на workload.

| S mode | Retirements | Microclocks | MUL interval clocks, сумма | Demand beats | SPI transactions | SPI clocks |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 504 | 52936 | 38688 | 752 | 8 | 12288 |
| 1 | 504 | 106504 | 66216 | 1000 | 504 | 32128 |
| 2 | 512 | 107432 | 66464 | 1016 | 504 | 32384 |
| 3 | 512 | 133968 | 93000 | 1264 | 752 | 44288 |
| 4 | 512 | 107432 | 66464 | 1016 | 504 | 32384 |
| 5 | 512 | 133720 | 92752 | 1264 | 752 | 44288 |
| 6 | 504 | 116176 | 75888 | 1248 | 504 | 36096 |
| 7 | 504 | 142712 | 102424 | 1496 | 752 | 48000 |

Восемь отдельных register edge workloads проверяют зависимость цикла от данных:

| R × S, hex | RAM MUL interval | FRAM+prefetch MUL interval | FRAM+prefetch loop CPI |
|---|---:|---:|---:|
| 0000 × 8000 | 111 | 142 | 98.142857 |
| 0001 × 7fff | 111 | 142 | 98.142857 |
| 0001 × 8000 | 114 | 145 | 99.619048 |
| 0002 × 4000 | 113 | 144 | 99.126984 |
| ffff × 8000 | 147 | 178 | 115.857143 |
| 8000 × 8000 | 116 | 147 | 100.603175 |
| ffff × ffff | 145 | 176 | 114.873016 |
| 7fff × 7fff | 140 | 171 | 112.412698 |

В отдельной differential suite ordinary RR (R0/R1, S=R2, без T/IRQ) latency
от начала fetch до retirement составила **109–150 тактов RAM**, **214–252 FRAM**.
Это холодный одиночный instruction test, другой интервал, чем warmed loop.
Длительность зависит от popcount битов R, знаков и формирования flags;
число итераций всегда 16. Полные JSON содержат и счётчики всех старых workloads.
Все 1018 прежних benchmarks побайтно сохранены. AM4 MUL был источником
sequencing, но сопоставимого измеренного AM4 MUL CPI/Fmax в материалах нет;
численного ускорения относительно AM4 здесь не заявляется.
