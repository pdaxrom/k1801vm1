# CP27: FIS

**CP27 завершён:** оба fit, FIS portable/vendor parity, integer regression,
formal datapath proof и negative controls прошли. MMU отсутствует.
[Manifest](verification-cp27.json), [исходные счётчики](benchmarks-cp27.json).

FADD `07500R`, FSUB `07501R`, FMUL `07502R`, FDIV `07503R` используют B по
Rn/Rn+2 и A по Rn+4/Rn+6. Результат заменяет A; после двух успешных записей
Rn увеличивается на четыре. Все восемь Rn, включая SP/PC, допустимы.

## Документированный арифметический профиль

Первоисточники: DEC [KE11-E/F User's Manual, §3.2.2–3.2.3, стр. 3-6…3-8](https://ftpmirror.your.org/pub/misc/bitsavers/www.computer.museum.uq.edu.au/pdf/EK-KE11E-OP-001%20KE11-E%20and%20KE11-F%20Instruction%20Set%20Options%20User%27s%20Manual.pdf)
и [Technical Manual, §3.2.3.1–3.2.3.4](https://ftpmirror.your.org/pub/misc/bitsavers/www.computer.museum.uq.edu.au/pdf/EK-KE11E-TM-002%20KE11-E%20and%20KE11-F%20Instruction%20Set%20Options%20Manual.pdf).
Проверены также AM4 `mc.asm`, directed FIS tests в `lsi11-fpga` и прежний
`microcpu/ucode/j11_fis.asm` с его exact-reference corpus.

F-format содержит знак, excess-128 exponent и 23 хранимых fraction bits.
Exponent=0 означает ноль независимо от остальных битов. Результат округляется
к ближайшему; точная половина увеличивает модуль. FADD/FSUB при результате
ниже диапазона дают чистый ноль. FMUL/FDIV в таком случае оставляют операнды
и Rn и переходят через 0244 с NZVC=012. Overflow даёт 002, division by zero
(включая 0/0) — 013. Верхние поддержанные PSW bits сохраняются в trap frame.

Выбран точный арифметический результат F-format. Это **не воспроизведение
микротактов и двух guard bits KE11-F**: его shortcut при разности exponents
более 24 и промежуточное усечение не копируются. uJ11 хранит шесть guard bits
со sticky alignment; независимый эталон вычисляет точные Python Fraction и
округляет один раз. Отличие алгоритмов зафиксировано явно, диагностическая
совместимость с историческим KE11-F пока не заявляется.

Прежний microcpu использует FIS compatibility extension с underflow trap для
всех четырёх операций; его corpus нельзя переносить без проверки этого
различия. `core/core.c` использует host float и не является FIS oracle этого
checkpoint. Этот C core здесь не изменяется.

## Реализация и размещение

Реализация: **223 слова FIS + 31 linking JUMP = 254 новых слова**;
весь ROM **954/1024×36**, свободно 70 слов. Все 700 baseline words и прежние
метки сохранены на своих адресах. `microcode/fis.uasm` остаётся читаемым;
`tools/link_fis.py` размещает fall-through blocks в свободных участках и
проверяет результат существующим microassembler. Generated hex вручную
не редактируется.

Encoding v12 расширяет pair5 из Z/Q в D/Q. Имя `ZQ` разрешено только с D=ZERO
и сохраняет все прежние encodings. `DQ` даёт `Q+1`, `Q|1`, `Q+32` за один
ALU cycle. Дополнительных registers, FF, loop counter, multiplier/divider или
EBR нет. T0…T7 и Q содержат operands, exponent, signs, original NZVC и счётчик.
FMUL/FDIV выполняют по 30 итераций; microclock counts приведены в
[benchmarks](benchmarks.md#cp27-fis-на-ram-и-spi-fram).

Все четыре operand READ выполняются до временного изменения flags. Перед
WRITE восстановлен исходный PSW. Ошибка второго WRITE сохраняет первую
успешную запись, но не меняет Rn/NZVC. Ошибки памяти используют прежний 004
trap; IRQ/trace принимаются только на существующей instruction boundary.
Память — MR45V100A SPI FRAM; transport и prefetch не изменяются.

## Первый synthesis

| Gate | Scope | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---|---:|---:|---:|---|---:|
| CP27a | core + probe | 863 | 299 | 4 | 35 MHz PASS | 35.954 MHz |
| CP27b | core + FRAM/prefetch/IRQ + probe | 1095 | 416 | 4 | 29.56 MHz PASS | 31.300 MHz |

Оба fit используют LCMXO2-1200HC-4SG32C и прежние probes/strategy/clock
constraints. Относительно CP26 оба mapped designs меньше на 4 LUT; это не
изолированная стоимость D/Q или decoder. Полные UART/timer/panel/SD/RK и
external pin timing ещё не включены. FPGA не программировалась.

AM4 помещает FIS с периферией в свой board top, но у него другой формат ROM,
sequencing и bus hooks. Прежний microcpu FIS добавлял 389 слов к вертикальному
3584-word store без новой аппаратуры. Эти размеры нельзя прямо переносить
на 36-bit uJ11. Следующий resource baseline должен включить весь board top.

## Проверки

Все 23840 cases прошли на RAM/FRAM × portable/vendor; CSV побайтно равны,
включая microclocks, memory beats и SPI counters.

Основной corpus — 23840 независимых ожидаемых состояний: 9568 арифметических
пар (5472 directed и 4096 reproducible random), 10752 комбинации R0…R7,
NZVC, trace/IRQ, 3072 fault injection на четырёх READ и двух WRITE и
448 odd-address cases. В последних 64 случая с нечётным SP явно проверяют
terminal second fault. После каждой инструкции сравниваются все R0…R7,
PSW, IRQ acknowledge, fault status, последовательность адресов/read/write/
data/error на шине и конечная память. RAM использует 0…3 wait states.

Trace сохраняет прежний CP17/DCJ11 профиль: arithmetic trap при исходном
T=1 сопровождается trace frame до исполнения handler opcode. Memory fault
имеет приоритет над trace/IRQ. При odd SP внутри trap vector PSW уже загружен,
SP уменьшен, первая запись frame запрещена и новый PC ещё не установлен.
Эти результаты заданы отдельной ISA-моделью, без чтения RTL или microcode.

Пятнадцать намеренных изменений в arithmetic, rounding, sticky, iteration
count, commit order, flags, D/Q и decoder отвергнуты настоящим RTL. Перед
мутациями исходный ROM проходит тот же corpus из 5616 случаев.

Yosys доказывает эквивалентность старого и нового datapath при D=0 для pair5;
остальные inputs/state не ограничены. Все восемь прежних ALU words с pair5
имеют D=0. Control words не записывают ALU result. Вариант с потерянным carry
не проходит доказательство. Decoder miter проверяет все 65536 opcodes:
отличаются только 32 новых FIS encodings.

Свежая portable integer-регрессия: **268843 normal + 49596 fault cases** на
каждой RAM/FRAM. Все 48 CSV остальных 24 suites побайтно равны CP26. Для
illegal удалены 64 FIS cases, оставшиеся 4096 совпадают после перенумерации;
для trace_bit ровно 64 reserved probes перенесены с 075000 на 075040 при
неизменных clocks/memory beats. Старые C fixtures остальных 24 suites
побайтно сохранены; `core/core.c` не менялся. Прежняя полная vendor integer
регрессия и 1146 benchmarks остаются результатами CP26: повторный прогон
этого объёма в CP27 не заявляется.

Portable FIS использует Verilator, vendor — Icarus с неизменёнными
DP8KC/GSR/PUR Lattice. У vendor model есть procedural assign/deassign,
которые Verilator здесь не поддерживает. Для strict Verilator lint лишь
в build-копии старого SPI model сделано явным его прежнее unsigned widening;
оригинальный файл и Icarus model сохранены. Vendor run можно разбить на
четыре независимых процесса: исходные case IDs, полные raw logs и каждый
CSV сохраняются, aggregate создаётся только после успеха всех частей.
Сохраняются десять Icarus warnings об implicit wires внутри исходного
DP8KC.v; RTL проекта и portable FIS builds таких предупреждений не имеют.

```sh
make all test-fis test-fis-negative benchmark-fis
make vendor-fis vendor-fis-benchmark
make test-datapath-equivalence YOSYS=/path/to/yosys
make verify-cp27 YOSYS=/path/to/yosys
```

Yosys можно установить по pinned [formal-requirements.txt](../tools/formal-requirements.txt),
как в [CP23](area-sequencer.md). FIS oracle содержит только integer/Fraction
arithmetic; никаких host IEEE float expected values.

## Следующая граница проекта

Свободны **70/1024 control-store words и 3/7 EBR**, до физического LUT limit
—185. Это измеренный остаток текущего probe scope. Полный FP11 в эти 70 слов
не спроектирован и не синтезирован; обещать его fit на основании размера FIS
нельзя. Сначала нужен [общий HC1200 top и запуск RT-11](hc1200-integration.md).
FIS не добавляет MMU, банков RF или processor modes.
