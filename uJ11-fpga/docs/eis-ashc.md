# CP22: ASHC через существующие ALU и Q

CP22 — завершённый ASHC checkpoint, без MMU. Исправленные CP22c/d прошли
MAP/PAR/TRACE и полную portable/vendor регрессию.
[Manifest с hashes](verification-cp22.json).
MUL/DIV/XOR не добавляются до отдельного исследования площади: FRAM scope
занимает 1098 LUT, лишь 2 LUT ниже желательных1100.

## Архитектура и microcode

RF остаётся 16×16 с двумя чтениями и одним write port. ALU 16, Q 16, IR, PSW,
sequencer, память и prefetch не меняются. Нет аппаратного счётчика, barrel
shifter, multiplier/divider или дополнительного состояния.

Добавлена 41 микрокоманда:592/1024×36, encoding v11. Все 235 прежних labels сохранены; 549 words неизменны, 019/01a
переставлены для исправления ASH alias ordering. Единственное функциональное изменение RTL — decoder:
ASH072 и ASHC073 объединены сравнением `IR[15:10]==035`; bit 9 выбирает
entry 019 либо 01c. Полный мiter проверяет все 65536 opcodes против архива CP21a;
ровно 512 encodings 073000..073777 получают новую entry.

| Routine | uaddress | Назначение |
|---|---|---|
| ASHC | 01c–01f | CALL общего EA; R[Rs]→T4, R[Rs\|1]→Q; OR_MD |
| ASHC_OPERAND | 290–297 | RD→T0 либо word READ по T1 |
| ASHC_COUNT | 2dc–2e1 | T6=0; count&63; zero/direction branch |
| ASHC_RIGHT_LOOP | 2e2–2e7 | Q→T5, LSR для C, RFQ_R, DEC, CJUMP |
| ASHC_LEFT_LOOP | 2e8–2ed | LSL для C/V, RFQ_L, sticky V, DEC, CJUMP |
| ASHC_COMMIT | 2ee–2f7 | high/low writeback, итоговые N/Z, восстановление V/C |
| ASHC_MDR | 2f8 | MDR→T0; PAGE к COUNT |

T4:Q содержит одну 32-bit величину. Оба слова читаются после count EA.
До успешного operand READ архитектурные flags не меняются; EA side effects
сохраняют прежний порядок. Слева Q15 переходит в high bit0; справа high bit0
переходит в Q15, знак high распространяется. Combined RF/Q shift не обновляет
flags от shifted writeback, поэтому отдельная ALU microinstruction формирует
carry и overflow. Слева T6 накапливает V за все сдвиги. DEC обновляет NZV,
сохраняя последний carry.

High записывается в Rs, затем low в Rs|1. Для нечётного Rs оба адреса совпадают,
low выигрывает. N/Z формируются по полному 32-bit результату T4:Q, независимо от
последующего alias двух записей. Отдельный parity decoder не нужен.
T/IPL сохраняются. Trace/IRQ принимаются после final flags LOAD/FETCH.
Запись PC использует прежний prefetch redirect.

## Источники и границы профиля

[DEC J-11 User Guide, Oct1983](../../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf),
printed6-41 (PDF page140), задаёт32-bit operand, count−32..+31 и flags результата.
Страница прочитана визуально. Регистровая пара остаётся32-bit операндом и при
odd Rs: входное слово дублируется, а low store выигрывает. Rotate при правом
сдвиге — следствие этого дублирования, для первых 16 шагов. Это ограничение
явно уточняет [DEC KE11-E/F manual, §2.1.3](https://bitsavers.org/pdf/dec/pdp11/1140/DEC-11-HKEFA-A-D_KE11-E_and_KE11-F_Instruction_Set_Options_Manual_197303.pdf).
Для17..32 шагов действует общая 32-bit arithmetic-shift операция; аппаратный
16-bit rotate modulo16 сюда не подставляется. N соответствует bit31 результата,
Z — проверке всех 32 bits, до alias writeback.

[SIMH PDP-11 executor, ASH/ASHC](https://github.com/simh/simh/blob/master/PDP11/pdp11_cpu.c)
читает count через ReadW/GeteaW до целевого регистра/пары и сохраняет high
результата для N/Z до последующего low store. Это дополнительная проверка
порядка operand access и трактовки 32-bit flags, а не замена документации.

В [нашем C executor](../../core/core.c) найдены и исправлены две ошибки DCJ11:
раннее чтение целевого регистра в ASH/ASHC и вычисление ASHC N/Z после
aliased stores. [Точный patch](cp22-core-fix.patch) ограничен case0072/0073;
поведение других CPU models сохранено. Instrumentation добавляет только
наблюдательные hooks в build copy уже исправленного executor.

Пример ASHC (R0)+,R0: R0=7ffe,R1=0001, count−32 по адресу7ffe.
После вычисления EA и чтения count R0=8000; сдвигается 8000:0001, результат ffff:ffff,
NZVC=1001. Для -(R0), исходного R0=8000 и count−32 по адресу7ffe
сдвигается7ffe:0001, результат0000:0000,NZVC=0100. ASH использует тот же
порядок. Для ASHC с нечётным Rs=1,R1=8000,count−1 low store даёт4000,
но N=1: полный результатc000:4000 отрицательный.

256 отдельных positive C cases охватывают оба знаковых перехода, even/odd Rs
и все 64 counts. Независимая bit-by-bit модель проверяет исходные C records;
**расхождения запрещены, ожидаемые результаты не переписываются**. Затем те
же записи проверяются RTL с RAM/FRAM и portable/vendor ROM. Все 256 проходят. Дополнительно проверены 17408 register cases по независимой
32-bit модели и 14 ASH alias cases с count0.
Существующий полный набор `tests/core_tests.c` также проходит с ENABLE_MMU=0.

AM4 entry2a0 и общая ASH/ASHC routine исследованы:
[AM4 microcode](../../lsi11-fpga/ucode/experimental/am4/mc.asm),
[AM4 fields](../../lsi11-fpga/ucode/experimental/am4/tools/am29_m4.def).
Он сохраняет low word в R12 до count fetch и обрабатывает high позднее.
Этот порядок не копируется. Используются существующие uJ11 RFQ_L/R;
отдельный AM4 timer не добавляется.

CP22a/b, а также прежний ASH snapshot CP21, отвергнуты как основание для
alias semantics: совпадение с ошибочным oracle не доказывает корректность.
Их исходники и synthesis reports остаются историческими материалами;
текущие gate — только CP22c/d.

## Verification

На каждом RAM/FRAM × portable/vendor сочетании прошли 231105 normal instruction
cases и 35836 fault frames; отдельно — 256 positive alias cases. Исправленная
ASH даёт 26328 completed cases из 27204 candidates, с 876 явными exclusions.
Все 176281 прежних non-EIS cases сохранены. Для 34812 прежних fault frames
сохранены архитектурные результаты и bus beats; timing ASH faults изменился.
Все 986 benchmarks на ROM-модель прошли. Из 890 прежних workloads 871
сохраняет counts; 19 ASH/prefetch workloads изменили timing/SPI activity. Побайтная сверка ROM models: 40 cycle CSV +
64 benchmark JSON, дополнительно две пары alias CSV (RAM и FRAM).
Проверены 94 synthesis source archives и 371 raw report hashes; текущие fit
inputs совпадают с архивами CP22c/d. [Полные данные](benchmarks-cp22.json).

28496 completed normal C fixtures /29300 candidates;804 exclusions:
abort320, vector456, I/O268, stack384, odd0 (причины пересекаются).
Матрица включает15 32-bit edges×64 counts×16 NZVC, ignored high count bits,
все addressing modes и504 normal encodings, SP/PC, trace и eligible/masked IRQ,
2048 дополнительных odd-pair cases и low/high EA aliases.

Отдельно 1024 completed fault frames: 752 ACK errors и 272 odd-word, exclusions 0.
Восемь always-fault encodings `ASHC @-(PC),Rn` покрыты для всех Rs: pointer
читается из самого нечётного opcode. Normal+fault union покрывает512 encodings.
C после captured BUSERR frame не меняет state и не добавляет bus accesses.
FRAM проверяется реальной SPI model из frozen lsi11 peripheral sources.

13 negative controls требуют наблюдаемого architectural/bus mismatch:
missing opcode, high count mask, zero C, count sign, sticky V, Q transfer left,
right carry, odd-pair N/Z, early high/low snapshots, flags before count READ, отдельно odd-pair Z
и старый неправильный ASH snapshot из архива CP21a.
Они используют архивированные fit sources и фактические C fixtures.
Дополнительно все 8 ASH negative controls обновлены под исправленный
C reference и CP22c: неверные mask/count/C/V, logical right, ранний snapshot,
flags до READ и отсутствующий opcode. Прежний тест, ошибочно отклонявший
позднее чтение регистра, исправлен. Всего 21 negative control run.

Команды: `make test-eis-ashc test-eis-ashc-fault`, `make benchmark-eis-ashc`,
`make test-ashc-alias`, `make verify-cp22`.

## HC1200 baseline

Diamond3.14.0.75.2/Synplify, LCMXO2-1200HC-4SG32C, full MAP/PAR/TRACE:

| Archive | Scope | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---|---:|---:|---:|---|---:|
| [CP22c](../synth/reports/cp22c/result.json) | core+generic IRQ+probe | 856 | 299 | 4 | 35 MHz PASS | 37.258 MHz |
| [CP22d](../synth/reports/cp22d/result.json) | FRAM/prefetch+KW11/KL11 resolver+probe | 1098 | 416 | 4 | 29.56 MHz PASS | 30.457 MHz |

CP21a/b:849/1094 LUT, те же FF/EBR, TRACE36.876/31.309 MHz.
Прирост+7/+4 LUT. FRAM path32.859ns,22 logic levels,54.6% routing:
EBR→register selector→address comparison/match/buffer→ACK/fault redirect→uPC→EBR.
Снижение TRACE Fmax0.852 MHz связано с этим фактическим path; timing29.56
проходит с setup margin0.996ns. Изменения placement нельзя приписать одному
элементу RTL без нового эксперимента.

До физических 1280 остаётся182 LUT; до желательных 1100 —2. Полные
UART/timer/panel/SD/RK и внешние pin delays в fit не входят.50 MHz не достигнуты;
FPGA не программировалась. Прежде следующей EIS инструкции нужен area gate.

MAP разбивает 1098 LUT на 984 logic, 48 distributed RAM и 66 ripple logic.
Synplify сохраняет RF как 4 DPR16X4C + 4 SPR16X4C, microstore — 4 DP8KC.
Это суммарные категории отчёта, а не оценка площади отдельных RTL modules.
Первое направление следующего area gate — общие combinational controls
sequencer/ACK и register selectors. Экономия пока не измерена; изменение
принимается только после отдельного A/B fit и проверки прежней ISA/FRAM.

## Производительность

Отдельная ASHC с count в register и ideal FETCH:
0 —21 clocks; left n=1..31 —24+5n; right n=1..32 —25+5n.
Примеры +1=29,+15=99,+31=179,−1=30,−31=180,−32=185.
Дополнительные memory stalls измеряются отдельно.

24 benchmarks×4 memory modes:31 ASHC+BR,32 warmup retirements исключаются,
затем256 measured retirements. Count в register/immediate/(Rn), значения
0,+1,+15,+16,+31,−32,−31,−1. Проверяются оба результата и PSW, PC/SP,
отсутствие записи count operand. Это CPI смеси, не latency отдельной ASHC.
Ideal pair loops быстрее FRAM при коротких shifts; длинные shifts ограничены
пятью microclocks на один bit. Immediate пока использует общий data READ.

Register loop с count +1 на FRAM/prefetch: **42.09375 CPI**;
рассчитанная скорость смеси при 29.56 MHz — **0.702242 MIPS**.
[Все 96 новых измерений](benchmarks-cp22.json), [таблица CPI](benchmarks.md).

Сопоставимых measured ASHC CPI для прежних microcpu/AM4 не найдено.
Исторические AM4 board1058/393/7 и microcpu1095/431/7 имеют другой scope:
[previous experiments](previous_experiments.md). Численный speedup не заявляется.
