# CP21: ASH через существующий datapath

**Исправлено в CP22:** описанный ниже snapshot до EA был ошибкой C oracle.
Текущий ASH читает целевой регистр после count EA; исправлены words019/01a
и DCJ11 executor.26328 completed cases,876 exclusions.
[Исправление и проверка по документации](eis-ashc.md).

Ниже — исторический отчёт CP21, не спецификация исправленного ASH.

**CP21 выполнен:** полная portable/vendor регрессия и HC1200 fit проверены.
Это отдельный EIS gate только для `ASH`; `MUL`, `DIV`, `ASHC`, `XOR` остаются
следующими этапами. Адреса 16 bit, logical == physical, один kernel register
set; MMU отсутствует.

## Исходные алгоритмы

[Существующий DCJ11 executor](../../core/core.c) — `case 0072` (ASH), рядом
`0070/0071/0073/0074` (MUL/DIV/ASHC/XOR). ASH сохраняет значение регистра
**до** DECODE_DST, затем читает word-операнд и берёт только его bits5:0.
Значения 0..31 означают сдвиг влево, 32..63 — вправо на 64−count.
При count=0 V=C=0; справа знак распространяется, V=0, C — последний
вышедший бит. Слева V отражает любую потерю знака за весь сдвиг.
Это профиль фактического C oracle; его исходник и ISA logic не изменялись.

[AM4 microcode](../../lsi11-fpga/ucode/experimental/am4/mc.asm): entry298/2a0
для ASH/ASHC, общая подготовка в2a1, шестибитная маска и LDCT в2a4,
OR_TC в2a7; MUL entry2b0 и DIV entry2c8. AM4 использует Q, совместные
RF/Q shifts, аппаратный timer/counter и OR_TC по timer bit5/сохранённому carry.
[Формат AM4](../../lsi11-fpga/ucode/experimental/am4/tools/am29_m4.def)
задаёт LDCT/RFCT, CCT и ASHCR/ASHCL. В uJ11 сохраняется принцип serial
ALU + microcode; дополнительный counter и AM4 56-bit формат не копируются.

MUL/DIV требуют отдельного анализа signed/overflow и результатов нечётной
пары. У существующего C MUL пишет high в R, затем low в R|1; DIV имеет
отдельные zero/overflow paths. Эти операции не добавлялись в CP21.

## Microcode и flags

Всего **30 новых words**, **551/1024 ×36**, encoding **v11**. Все521 прежних
words и labels сохранены. Единственное функциональное изменение RTL —
predecode `IR[15:9]==072` → entry019, все512 encodings. Остальные RTL,
RF16×16, Q, ALU, PSW, sequencer и FRAM transport побайтно совпадают с CP20.

| Routine | uaddress | Действие |
|---|---|---|
| ASH | 019–01b | R[Rs]→T4; CALL общего DESTINATION_EA; OR_MD |
| ASH_OPERAND | 218–21f | Register→T0 либо word READ по T1 |
| ASH_COUNT | 262–266 | mask63, zero/direction branch; отрицательный count→64−count |
| ASH_RIGHT_LOOP / DONE | 267–26a | ASR, DEC count, CJUMP; итоговые NZV и writeback |
| ASH_LEFT_INIT / LOOP | 26b–273 | sticky V, LSL, DEC count, CJUMP; NZVC и writeback |
| ASH_MDR | 27f | MDR→T0, PAGE к ASH_COUNT |

T0 — count, T4 — рабочее значение, T6 — накопленные flags (в финале
используется только bit1=V). До успешного чтения count ни NZVC, ни целевой
Rs не меняются; обычные EA side effects идут в прежнем порядке.

Каждый правый шаг: ASR с NZVC, DEC с NZV (сохраняет C), CJUMP по Z.
Каждый левый шаг дополнительно OR-ит PSW в T6. DEC не может стереть
сохранённый shift overflow. В финале N/Z вычисляются от результата,
C остаётся от последнего сдвига, V берётся из T6; T/IPL сохраняются.
IRQ и trace обрабатываются в прежнем retirement boundary после финального
RF writeback. Изменение PC использует прежний prefetch redirect.

Отдельный hardware counter отсутствует; LOOPZ всё ещё привязан к0.
Q не нужен для одиночного 16-bit ASH и остаётся доступен будущим pair operations.
Нет multiplier/divider/barrel shifter или нового состояния.

## HC1200 synthesis

Diamond3.14.0.75.2 / Synplify, LCMXO2-1200HC-4SG32C, MAP/PAR/TRACE:

| Archive | Scope | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---|---:|---:|---:|---|---:|
| [CP21a](../synth/reports/cp21a/result.json) | core+generic IRQ+probe | 849 | 299 | 4 | 35 MHz PASS | 36.876 MHz |
| [CP21b](../synth/reports/cp21b/result.json) | FRAM/prefetch+KW11/KL11 resolver+probe | 1094 | 416 | 4 | 29.56 MHz PASS | 31.309 MHz |

Относительно CP20: +13/+9 LUT, те же FF/EBR. FRAM critical path
EBR→RF address selection→FRAM address/ACK/error→conditional next-address→EBR:
31.966 ns,21 logic levels,54.2% routing. Это реальный path отчёта,
не гипотеза о barrel shifter или аппаратном умножителе.

Остаются6 LUT до желательных1100 и186 до физических1280. 50 MHz не достигнуты.
Полный UART/timer/panel/SD/RK board top и внешние pin delays в fit не входят.
Поэтому CP21 принимается как ограниченный gate; расширять EIS без новых
измерений нельзя. RF/ALU/формат пригодны для serial ASH без новых FF;
оптимальность всего J-11 core пока не установлена.

## Verification matrix и ограничения oracle

26316 завершённых C fixtures из27204 кандидатов:504 normal encodings,
все64 counts,15 operand edges×16 NZVC, отдельно bits15:6 count,
все8 addressing modes, Rs/Rd aliases, SP/PC, immediate/absolute,
signed и wrapping indexed extensions, trace и eligible/masked IRQ.
888 кандидатов исключены явно: abort320, vector336, I/O524, stack204;
причины пересекаются. Эти случаи не считаются реализованными.

Отдельная ASH fault matrix:1024 завершённых vector004 frames,
752 ACK errors и272 odd-word cases, без exclusions. В этой группе исходный
C после captured frame не совершает дополнительных state changes/bus accesses.
Восемь encodings `ASH @-(PC),Rn` всегда получают odd-word fault: после
предекремента PC из памяти читается само нечётное слово opcode как pointer.
Они покрыты отдельными fault cases для всех Rn; вместе проверены все512 encodings.
Итоговый аудит выявил отсутствие шести таких register variants в первоначальной
fault matrix; добавлены24 cases, RTL и synthesis не менялись.
Проверяется точная последовательность reads/writes, R0..R7, PC/SP, PSW,
retirement, IRQ и wait states. FRAM проходит реальную SPI model, а не RAM delay.

Восемь negative controls обязаны проваливаться: отсутствующая ASH,
неигнорируемые high count bits, C при count0, потерянный sign count,
ненакопленный V, logical right вместо ASR, поздний snapshot Rs и flags
до успешного READ. Используются архивированные fit sources и те же C fixtures.

Команды: `make test-eis-ash test-eis-ash-fault`, `make benchmark-eis-ash`,
`make verify-cp21`; vendor ROM — через LATTICE_SIM_DIR или build/vendor.
Итог:202597 completed instruction cases +34812 fault frames на каждом
RAM/FRAM×portable/vendor сочетании. Все176281 прежних cases,33788 прежних
fault frames и794 прежних benchmark counts сохранены. Всего890 benchmarks/ROM,
96 portable/vendor result files совпали побайтно;90 source archives и355 raw
report hashes проверены. [Manifest](verification-cp21.json), [cycles и benchmarks](benchmarks-cp21.json).

## Измеренные microclocks

С ideal FETCH отдельная register-count ASH:
count0 —10 clocks; слева n=1..31 —16+4n; справа n=1..32 —13+3n.
Например +1:20,+15:76,+31:140,−1:16,−31:106,−32:109.
Счёт включает opcode FETCH; memory stalls отдельно.

Benchmarks:24 workloads ×4 memory modes. Каждая loop содержит31 ASH+BR,
один warmup loop исключён, затем256 retirements; count в register,
immediate либо memory. Это CPI смеси, не latency отдельной ASH.
Проверяются итоговые registers/PSW и отсутствие записи count operand.

FRAM prefetch для register count0/+1/−1 даёт40.15625 CPI loop,
для +15 —76.96875, для −32 —108.9375. CPU/SPI nominal29.56/14.78 MHz.
Существующая mode2 destination EA читает immediate count общим data READ:
отдельный instruction-stream fast path для ASH immediate здесь не добавлялся.

Прежние AM4 board1058/393/7 и microcpu1095/431/7 имеют другой scope;
их raw результаты разобраны в [previous_experiments.md](previous_experiments.md).
Сопоставимых фактических ASH CPI benchmarks там нет. Выигрыш uJ11 по ASH
относительно AM4/microcpu численно не заявляется.
