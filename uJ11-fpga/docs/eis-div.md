# CP26: микрокодный DIV

**CP26 завершён: synthesis и полная portable/vendor регрессия прошли.**

## Семантика и источники

DIV `071RSS` делит signed 32-bit R:R|1 на signed 16-bit S, округляя частное
к нулю. Остаток имеет знак делимого. При успешном делении R получает частное,
R|1 — остаток; N/Z описывают частное, V/C очищены. Деление на ноль и частное
вне −32768…32767 сохраняют оба регистра после побочных эффектов адресации.

[DEC J-11 User Guide, печатная стр. 6-42](../../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf)
требует **чётный R**. Для нечётного R здесь выбран отдельный программный
профиль: сначала запись частного, затем остатка в совпадающий R|1. Это
детерминированное расширение по примеру SIMH, **не гарантия поведения
аппаратного DCJ11**. Объединённые 512 opcode tests включают 256 документированных
чётных encodings и 256 encodings этого расширения.

[Официальный SIMH PDP11 executor](https://github.com/simh/simh/blob/master/PDP11/pdp11_cpu.c),
case DIV: S читается до делимого; при S=0 NZVC=0111. При переполнении
Z/C=0, V=1, N соответствует знаку математического частного. Отдельный
INT32_MIN/−1 даёт NZVC=0010. На успехе остаток записывается последним.
Узкий C patch и независимые тесты фиксируют именно этот профиль.

[DEC KE11-E/F Options Manual](https://bitsavers.org/pdf/dec/pdp11/1140/DEC-11-HKEFA-A-D_KE11-E_and_KE11-F_Instruction_Set_Options_Manual_197303.pdf),
§4.7.3.4, описывает итерационную shift/add/sub реализацию DIV. Рассмотрен и
[наш AM4 microcode](../../lsi11-fpga/ucode/experimental/am4/mc.asm):
2c8/2c9 выбирают S через OR_MD; 152/153 читают R и R|1; 175–177 и 2cc–2d7
ведут подготовку/цикл с совместным ALU+RAMQU shift; 17b–17f и 2dc–2e7
корректируют результат. uJ11 использует собственный restoring loop, который
помещается в существующий datapath без специального DIV shift mode.

## Реализация

**58 новых слов / 16 меток; 700/1024×36, 68.359375%, 271 метка, encoding v11.**
Все 642 слова и 255 меток CP25 сохранены. Из RTL изменён только decoder:
общий `IR[15:10]==034` распознаёт MUL/DIV, `IR[9]` напрямую добавляет 0x10
к entry, а `|IR[5:3]` выбирает register или прежний EA path. Новых ALU ops,
RF ports, FF, EBR, полей microinstruction или assembler syntax нет.

| Адреса, hex | Назначение | Слов |
|---|---|---:|
| 072–073, 076–077, 07a–07b | S register или EA/READ/MDR | 6 |
| 181–18a | позднее делимое, сохранение знаков, S=0 | 10 |
| 256–25e | модули операндов, раннее переполнение, счётчик | 9 |
| 3b5–3bf | 16 итераций restoring divide | 11 |
| 308–30e | signed range и знак частного | 7 |
| 328–32c | знак остатка и commit | 5 |
| 348–34e, 368–36a | flags исключительных результатов | 10 |

T2 хранит модуль S; T4:Q — модуль делимого, затем остаток/частное.
T0 сохраняет знак делимого, T3 — XOR знаков, T1 — счётчик, T5 — единицу.
Отрицательное T4:Q преобразуется через SUB low и SBC high с borrow.

Если начальный T4 ≥ |S|, модуль частного не помещается даже в 16 bits;
регистры результата не записываются. Иначе каждая из 16 итераций сдвигает
T4:Q влево на один, вычитает |S| и либо восстанавливает T4, либо устанавливает
младший бит Q. Инвариант T4 < |S| ≤ 32768 гарантирует, что сдвинутый остаток
помещается в 16 bits. Затем проверяется предел +32767/−32768 и восстанавливаются
знаки. Итоговый PASS Q формирует NZVC независимо от промежуточного overflow
при NEG 0x8000. Число итераций и temporaries задаются только микрокодом.

Flags внутри цикла временные; IRQ/trace принимаются на прежней instruction
boundary после окончательных NZVC. До успешного operand READ не изменяются
ни flags, ни R:R|1. Используются существующие fault/EA repair и read-only bus
operation. Transport **MR45V100A SPI FRAM**, CSR semantics и prefetch сохранены.

## Исправление C oracle

[Patch](cp26-core-fix.patch) ограничен DIV при `model==DCJ11`:
позднее чтение делимого, возврат при operand abort, исправление zero/overflow
flags и явно описанное нечётное расширение. Другие CPU models сохраняют
прежнее поведение. Fixture results не редактируются вручную.

Проверка точного архивного CP25 C отвергает 112 even-R zero-divisor cases,
304 negative-overflow cases, 576 odd-extension results и 128 случаев
позднего чтения младшего слова. Это диагностические подмножества ordinary
records. В 2368 из 4000 fault cases старый C меняет register/PSW после
сформированного frame; дополнительных bus beats нет. Исправленный C
не продолжает ни один из этих fault cases.

## Проверки

Новые fixtures: **13664 завершённых / 13848 кандидатов**, 184 exclusions
(abort 128, vector 128, I/O 56; причины пересекаются; odd/stack 0).
Fault fixtures: **4000 frames**, из них 3008 ACK error и 992 odd-word;
exclusions 0. Ordinary/fault union покрывает все 512 encodings и восемь modes.
128 специальных ordinary cases меняют low dividend word через (R1)+/−(R1).

Независимая Python arithmetic модель сравнивает 861968 пар: 13 крайних
32-bit dividends × все 65536 divisors и 10000 seeded random pairs.
Дополнительная EA/ISA модель проверила **7322 ordinary records**,
включая 385 aliases R=S и 128 специальных low-word aliases, без T/IRQ;
полные trace/IRQ frames сравниваются с исполнением настоящего C oracle.
15 negative controls проверяют ошибочные signs, zero/overflow, потерю borrow,
quotient bit, одну пропущенную итерацию, раннее чтение и premature flags.
CSR test содержит 2048 cases: восемь арифметических границ, все начальные
NZVC, read-clear/no-write, ACK/odd faults и waits 0…3.

## Реальный synthesis HC1200

| Scope | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---:|---:|---:|---|---:|
| [CP26a core + probe](../synth/reports/cp26a/result.json) | 867 | 299 | 4 | 35 MHz PASS | 37.151 MHz |
| [CP26b FRAM/prefetch/IRQ + probe](../synth/reports/cp26b/result.json) | 1099 | 416 | 4 | 29.56 MHz PASS | 31.771 MHz |

С теми же устройством, strategy, probes и clock constraints, что CP25:
+26/+10 LUT, без новых FF/EBR. Это разница целых mapped designs, а не
изолированная стоимость одного comparator. До 1100 остался **1 LUT**,
до 1280 — 181. Полные UART/timer/panel/SD/RK и external pin timing не включены.
50 MHz и предпочтительные 900–1000 LUT не достигнуты; дальнейшее расширение
должно предваряться снижением площади. FPGA не программировалась.

На длиннейшем пути CP26b: EBR → RF/address selection → I/O data selection →
opcode dispatch → sequencer → EBR; delay 31.501 ns, 16 logic levels,
60.6% route. Это измерение данного placement; оно не доказывает
универсальную оптимальность выбранного decoder. Сопоставимого измеренного
AM4 DIV CPI/Fmax в найденных материалах нет, ускорение относительно него
числом не заявляется. **MMU отсутствует полностью.**

## Benchmarks

16 workloads × ideal RAM / legacy FRAM / sequential FRAM / FRAM+prefetch.
Восемь modes делят 0xfffedcbb на 0x0123; остальные RR workloads проверяют
нулевой результат, signed limits, overflow, INT32_MIN/−1 и S=0.
Loop: 15 троек `MOV #high,R0; MOV #low,R1; DIV S,R0`, BR и, в modes 2…5,
MOV для восстановления R2. Один loop прогревается, восемь измеряются:
120 DIV и 368 либо 376 полных instruction retirements на workload.

CPI loop включает MOV и BR. DIV interval — от предыдущего retirement до
retirement DIV, включая fetch и operand waits. Demand beats, SPI clocks/CS
считаются отдельно. Производительность при 29.56 MHz вычисляется по
счётчикам функциональной симуляции FRAM, не по измерению платы.

Полная регрессия: 268907 normal + 49596 fault cases на каждом сочетании
RAM/FRAM × portable/vendor; 1146 benchmarks/ROM. Все 128 result files равны
между ROM models, все 120 прежних results и 24 C fixtures побайтно равны CP25.
Проверены 106 synthesis archives / 419 raw report hashes, lint, C regression,
unit/directed/peripheral tests и 65536 decoder encodings.
[Manifest](verification-cp26.json), [полные счётчики](benchmarks-cp26.json).

| S mode | RAM loop CPI | Legacy FRAM CPI | Sequential FRAM CPI | Prefetch CPI | DIV interval, prefetch | DIV/s в полном loop @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 52.217391 | 225.695652 | 114.826087 | 98.847826 | 157 | 97,515 |
| 1 | 54.173913 | 261.891304 | 173.195652 | 169.282609 | 268 | 56,941 |
| 2 | 53.702128 | 261.468085 | 171.765957 | 167.808511 | 269 | 56,219 |
| 3 | 54.340426 | 295.617021 | 205.914894 | 201.957447 | 376 | 46,713 |
| 4 | 53.702128 | 261.468085 | 171.765957 | 167.808511 | 269 | 56,219 |
| 5 | 54.021277 | 295.297872 | 205.595745 | 201.638298 | 375 | 46,787 |
| 6 | 54.826087 | 296.782609 | 185.913043 | 182.000000 | 307 | 52,962 |
| 7 | 55.478261 | 331.673913 | 220.804348 | 216.891304 | 414 | 44,442 |

| S mode | Retirements | Microclocks | DIV interval clocks, сумма | Demand beats | SPI transactions | SPI clocks |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 368 | 36376 | 18840 | 608 | 8 | 9984 |
| 1 | 368 | 62296 | 32160 | 728 | 248 | 19584 |
| 2 | 376 | 63096 | 32280 | 744 | 248 | 19840 |
| 3 | 376 | 75936 | 45120 | 864 | 368 | 25600 |
| 4 | 376 | 63096 | 32280 | 744 | 248 | 19840 |
| 5 | 376 | 75816 | 45000 | 864 | 368 | 25600 |
| 6 | 368 | 66976 | 36840 | 848 | 248 | 21504 |
| 7 | 368 | 79816 | 49680 | 968 | 368 | 27264 |

| Dividend / divisor, hex | RAM DIV interval | Prefetch DIV interval | Prefetch loop CPI |
|---|---:|---:|---:|
| 00000000 / 8000 | 125 | 156 | 98.521739 |
| 00007fff / 0001 | 139 | 170 | 103.086957 |
| ffff8000 / 0001 | 126 | 157 | 98.847826 |
| 00008000 / 0001 | 126 | 157 | 98.847826 |
| ffff7fff / 0001 | 132 | 163 | 100.804348 |
| 80000000 / ffff | 25 | 56 | 70.152174 |
| ffffffff / 0000 | 15 | 46 | 70.152174 |
| 00000001 / 8000 | 125 | 156 | 98.521739 |

Холодные ordinary RR (4806 cases, R0/R1, S=R2, без T/IRQ): от начала fetch
до retirement **15–149 тактов RAM**, **120–251 FRAM**. Эти интервалы
включают ранние выходы по нулю/переполнению и odd-R расширение. Они
не эквивалентны warmed loop interval и не означают постоянную latency DIV.
