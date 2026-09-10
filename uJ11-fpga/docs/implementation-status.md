# Implementation status, 2026-09-10

## CP31 — FIS, FP11 удалён; первый MMU checkpoint

FP11 и ODT отложены. FP RTL/dispatch/state/microcode/build options удалены;
954/1024×36 v12, 70 слов свободно. RK CSR теперь в 16 свободных словах
firmware EBR, верхний банк FRAM больше не занят периферией. Полный HC1200
top CP31c: **1252 LUT / 326 FF / 6 EBR / 628 slices / 30.917 MHz**, PASS.
Cold RT-11FB + DIR, FIS и portable/vendor storage checks прошли.

MMU пока **не подключён к CPU**. Изолированный 18-bit translator/PDR checker
прошёл 1769472 проверки; отсутствуют PAR/PDR storage, MMR registers,
MMU abort/restart и physical DMA extension. Пользовательский образ XM найден:
`../lsi11/disks/rt11v5.3/system.dsk`. Его загрузка и работа >64 КиБ ещё
не проверены. [Измерения, критерии готовности и дальнейшие gates](mmu.md).

Целевой профиль включает **18/22-bit mapping через MMR3<4>**, PAR16 и PA22.
Текущий 18-bit probe — промежуточный результат; 22-bit translation ещё не
реализована. MMU-off и оба включённых режима требуют отдельных проверок.

CP30 FP control/state сохранён в истории (`d59f19c`), полный FP11 не был
реализован. Плата остаётся CP29.

## CP29 — physical HC1200 bring-up

FLASH verification, real RT-11 boot/DIR and RGB/HDSP operation are confirmed. Keyboard codes and panel ESC are user-confirmed. HG read/write and file readback passed on real hardware at 1 kHz; the test daemon was stopped afterward. [Evidence and limits](board-bringup-cp29.md).

**CP28: полный board integration baseline синтезирован, RT-11/DIR прошли в RTL simulation.**
1217 LUT /318 FF /6 EBR /610 slices, 29.56 MHz PASS, TRACE 31.186 MHz,
полный MAP/PAR. Microcode 954/1024×36 v12, 349 labels, без изменений относительно CP27.

FRAM, UART/timer/panel/SD/RK, firmware ROM, reset и физический top включены.
ROM dispatch добавляет один внутренний FETCH clock; 65536 opcode проверены
с portable/vendor ROM. ALU эквивалентен CP27 по SAT и четырёхзначной симуляции.
KW11 timebase имеет прежний точный период при меньшей площади.

RT-11 cold boot + DIR: 355132188 clocks, 3270 UART wire bytes, 98 файлов,
162 SD reads/6 writes; backing image read-only. Прошли scoped integer/fault/FIS,
FRAM и периферийные проверки. [Подробный gate](hc1200-integration.md),
[manifest](verification-cp28.json).

**Ограничения:** всего 63 LUT/30 slices/1 EBR запаса, цель 900–1100 LUT не достигнута.
В общем top нет prefetch; физического программирования, external pin timing,
vendor whole-board RT-11, полного FP11 и register banking нет. MMU отсутствует.
Далее — площадь/prefetch, disk-error/file-write/Ctrl-C coverage, pin timing и плата.

## Исторический CP27

**CP27 завершён: FIS реализован и прошёл portable/vendor проверки.**
FADD/FSUB/FMUL/FDIV: **954/1024×36 v12, 349 labels**. 223 FIS words +31
linking JUMP, все 700 прежних words/271 label сохранены. D/Q расширяет
pair5; RF/Q/register state, sequencer и FRAM transport не расширены.

Core **863 LUT/299 FF/4 EBR**, 35 MHz PASS/TRACE **35.954 MHz**;
FRAM/prefetch/IRQ+probe **1095/416/4**, 29.56 MHz PASS/TRACE **31.300 MHz**.
−4 LUT к обоим CP26 gates, 0 FF/EBR. Полный board top не включён.

23840 FIS expected states проверяют exact F-format arithmetic, все восемь R,
NZVC, trace/IRQ, failed READ/WRITE, odd addresses и odd-SP second faults.
Прошли full portable/vendor RAM/FRAM; все четыре результата совпали побайтно.
24 FIS benchmarks/ROM уже совпали побайтно. 15 мутаций отвергнуты;
datapath legacy-context equivalence доказана с отрицательным carry control.
Свежая integer portable-регрессия: 268843 normal +49596 faults на RAM/FRAM.
Старые vendor integer/1146 benchmarks —исторический CP26, не новый прогон.

Следующий этап — [общий HC1200 top, bootstrap, RK service и RT-11](hc1200-integration.md).
Свободны 70 слов/3 EBR и 185 LUT до физического limit текущего probe scope.
Полный FP11 не реализован и его fit не доказан. Banking/native ODT,
stack-limit recovery и I/O timeout ещё предстоят. MMU отсутствует;
FPGA не программировалась. [FIS и источники](fis.md),
[manifest](verification-cp27.json), [измерения](benchmarks-cp27.json).

## Исторический CP26

**CP26 завершён: DIV во всех восьми S modes**, signed quotient/remainder,
zero/overflow flags и operand fault handling. 58 новых слов/16 меток;
**700/1024×36 v11, 271 labels**, все 642 слова/255 меток CP25 сохранены.
DEC требует even R; поведение odd R — явно описанное SIMH-style расширение.
RF/ALU/Q/sequencer/PSW/FRAM RTL не меняются.

Core **867 LUT/299 FF/4 EBR**, 35 MHz PASS/TRACE **37.151 MHz**;
FRAM/prefetch/IRQ+probe **1099/416/4**, 29.56 MHz PASS/TRACE **31.771 MHz**.
+26/+10 LUT к CP25, без новых FF/EBR. До 1100 остался 1 LUT;
полный board top и external pin timing не включены.

Прошли **268907 instruction cases + 49596 fault frames** на каждом
RAM/FRAM × portable/vendor сочетании, **1146 benchmarks/ROM**. Все 128
result files совпали; все 24 старых C fixtures, 48 cycle CSV и 72 benchmark
JSON побайтно сохранены относительно CP25. 106 synthesis archives и
419 raw report hashes проверены; текущие inputs совпадают с CP26a/b.
[DIV и исправления эталона](eis-div.md), [manifest](verification-cp26.json),
[измерения](benchmarks-cp26.json).

Следующий этап — снижение площади до интеграции полного board top.
Banking, native ODT, stack-limit recovery, I/O timeout и запуск ОС ещё
предстоят. 50 MHz и 900–1000 LUT не достигнуты. MMU отсутствует;
FPGA не программировалась.

## Исторический CP25

**CP25 завершён:** signed MUL, все восемь S addressing modes, odd-register
low-word writeback, full-product NZVC, late R read и operand fault handling.
45 новых слов/10 меток; **642×36 v11, 255 labels**. Все 597 слов/245 меток
CP24 сохранены; RF/ALU/Q/sequencer/PSW/FRAM RTL не меняются.

Core **841 LUT/299 FF/4 EBR**, 35 MHz PASS/TRACE **36.926 MHz**;
FRAM/prefetch/IRQ+probe **1089/416/4**, 29.56 MHz PASS/TRACE **30.672 MHz**.
До 1100 остаётся 11 LUT. Полный board top и external pin timing не включены.

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

Две ошибки существующего DCJ11 C MUL исправлены узким patch; отрицательный
контроль воспроизводит их на старом CP24 archive. Новый MUL проверен на
13110 normal/4000 fault cases, 7002 независимых records, 12 negative controls
и 1024 CSR cases/ROM. 861968 arithmetic pairs дополнительно проверены Python.

Следующий EIS этап — DIV. Banking, native ODT, stack-limit recovery, I/O
timeout, полный board top и запуск ОС ещё предстоят. 50 MHz и 900–1000 LUT
для FRAM scope не достигнуты. MMU отсутствует, FPGA не программировалась.
[Полный MUL checkpoint](eis-mul.md).

## Исторический CP24

**CP24 завершён:** XOR с поздним чтением source register по правилам J-11.
Все восемь destination modes используют прежний EA microcode; register mode
занимает один execution cycle. Добавлены пять слов и три метки: **597×36 v11**.
Все 592 прежних слова и 242 метки сохранены, C executor не изменён.

Core **854 LUT/299 FF/4 EBR**, 35 MHz PASS/TRACE **36.059 MHz**;
FRAM/prefetch/IRQ+probe **1089/416/4**, 29.56 MHz PASS/TRACE **30.273 MHz**.
Это +4/+2 LUT к CP23, без новых FF/EBR. До желательных 1100 осталось 11 LUT.
Внешние pin delays и полный UART/timer/panel/SD/RK в fit не включены.

Свежие **242133 instruction cases и 41596 fault frames** прошли на каждом
RAM/FRAM × portable/vendor сочетании; **1018 benchmarks/ROM**. Все 112
result files совпадают между ROM models; все 104 прежних result files и
20 C fixtures побайтно равны CP23. Дополнительно проверены 4698 независимых
XOR records, 385 alias cases, восемь negative controls и 1024 CSR cases/ROM.
Проверены 102 synthesis archives и 403 raw report hashes.

MUL/DIV, banking, native ODT, stack-limit recovery, I/O timeout и полный
board top ещё предстоят. 50 MHz и 900–1000 LUT не достигнуты. MMU отсутствует;
FPGA не программировалась. [XOR gate](eis-xor.md),
[manifest](verification-cp24.json), [benchmarks](benchmarks-cp24.json).

## Исторический CP23

**CP23 завершён:** общий target и low-bit OR-dispatch микросеквенсора.
Core **850 LUT/299 FF/4 EBR**, 35 MHz PASS/TRACE 36.302 MHz;
FRAM/prefetch/IRQ+probe **1087/416/4**, 29.56 MHz PASS/TRACE 30.193 MHz.
Это −6/−11 LUT к CP22, без новых FF/EBR. До желательных 1100 осталось 13 LUT.
Микрокод 592×36 v11, остальные RTL modules, C emulator и ISA сохранены.

74 formal equivalence points proven; неверный repair vector отвергнут.
Свежие 231105 instruction cases и 35836 fault frames прошли на каждом
RAM/FRAM × portable/vendor сочетании; 986 benchmarks/ROM. Все 104 result
files, 20 C fixtures и все microclock/memory/SPI counts побайтно равны CP22.
Проверены 100 synthesis archives и 395 raw report hashes.

Меньшая площадь выбрана по FRAM fit из трёх вариантов. Fmax немного ниже CP22;
29.56 MHz проходит с margin 0.709 ns. Полный board top, banking, native ODT,
MUL/DIV/XOR и остальные прежние ограничения сохраняются. 50 MHz и 900–1000 LUT
ещё не достигнуты. MMU отсутствует; FPGA не программировалась.
[Area gate](area-sequencer.md), [manifest](verification-cp23.json),
[benchmarks](benchmarks-cp23.json).

## Исторический CP22

**CP22 завершён:** полная portable/vendor регрессия прошла. ASHC через прежние ALU/Q;
исправлены ASH/ASHC destination capture после count EA и ASHC N/Z от 32-bit
результата до alias stores. Ошибки исправлены и в нашем DCJ11 C executor;
[patch](cp22-core-fix.patch). MMU отсутствует.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP22c core+probe | 856 | 299 | 4 | 35 MHz PASS, TRACE 37.258 MHz |
| CP22d FRAM/prefetch/IRQ+probe | 1098 | 416 | 4 | 29.56 MHz PASS, TRACE 30.457 MHz |

592 words v11 (+41), 549 прежних words и 235 labels сохранены;
019/01a исправлены. 21 negative control run (13 ASHC/alias + 8 ASH),
17408 независимых проверок 32-bit результата, 14 ASH alias checks и 256 ASHC sign-boundary cases без расхождений.
Существующая общая C core regression прошла. Полная проверка: 231105 instruction
cases и 35836 fault frames на каждом RAM/FRAM × portable/vendor сочетании;
256 alias cases проверены дополнительно. Все 986 benchmarks/ROM прошли,
871 прежний benchmark сохранён; 19 ASH/prefetch workloads изменили timing. Между portable и vendor совпадают 104 основных result files.
Дополнительно сверены две пары alias cycle CSV: по одной для RAM и FRAM.
Проверены 94 synthesis archives и 371 raw report hashes.
[Manifest](verification-cp22.json), [benchmarks](benchmarks-cp22.json).

CP22a/b отклонены по семантике, хотя timing проходил. Их отчёты сохранены
как история. До желательных 1100 осталось 2 LUT: перед MUL/DIV/XOR нужен
area gate. Native ODT, banking, stack-limit recovery, I/O timeout и полный
board top ещё предстоят; 50 MHz не достигнуты. FPGA не программировалась.
[ASHC и исправление oracle](eis-ashc.md).

## Исторический CP21; ASH alias ordering исправлен в CP22

**CP21 выполнен:** ASH во всех8 addressing modes, serial shifts через прежние
RF/ALU, без нового аппаратного состояния. Один kernel register set,
CM=PM=RS=0, NZVC/IPL/T;16-bit addresses, строго без MMU.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP21a core + probe | 849 | 299 | 4 | 35 MHz PASS, TRACE36.876 MHz |
| CP21b FRAM/prefetch/IRQ + probe | 1094 | 416 | 4 | 29.56 MHz PASS, TRACE31.309 MHz |

551/1024 words v11 (+30), все521 прежних words/labels сохранены.
Единственное функциональное изменение RTL — ASH opcode predecode.
202597 completed DCJ11 instruction cases на каждом RAM/FRAM×portable/vendor
сочетании;34812 fault frames. Новая группа:26316 completed/27204 candidates,
888 явных exclusions; отдельно1024 ASH fault frames без exclusions.
Все176281 прежних cases,33788 fault frames и794 benchmark counts сохранены.
890 benchmarks/ROM,96 byte-identical result files,90 synthesis archives/355 raw hashes.

ASH с ideal FETCH: count0 —10; left n —16+4n; right n —13+3n clocks.
FRAM register loops для0/+1/−1 —40.15625 CPI смеси31 ASH+BR.
До желательных1100 осталось6 LUT. Следующий gate должен контролировать
площадь до расширения EIS. MUL/DIV/ASHC/XOR, banking, native ODT,
stack-limit recovery, I/O timeout и полный board top ещё не реализованы.
50 MHz остаётся целью; FPGA не программировалась.
[ASH](eis-ash.md), [manifest](verification-cp21.json), [benchmarks](benchmarks-cp21.json).

## Исторический checkpoint CP20


**CP20 выполнен:** HALT restart-профиль существующего DCJ11 emulator и
синхронный peripheral RESET. Полный native console ODT отсутствует.
Один kernel register set, NZVC/IPL/T; 16-bit addresses, без MMU.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP20c core + probe | 836 | 299 | 4 | 35 MHz PASS, TRACE 36.647 MHz |
| CP20d FRAM/prefetch/IRQ + probe | 1085 | 416 | 4 | 29.56 MHz PASS, TRACE 30.593 MHz |

521 words v11 (+14), все 507 прежних words/labels сохранены. Новое поле
JUMP.init — зарегистрированный выход, пригодный для async reset UART.
FF+2 к CP19: регистр импульса и observation FF в probe.
176281 completed DCJ11 cases на каждом RAM/FRAM×portable/vendor сочетании,
33788 прежних fault frames, 64 HALT fault checks, 3 actual-peripheral scenarios.
Новые 6144 cases без exclusions; прежние exclusions остаются документированными.
794 benchmarks/ROM, все 786 прежних counts сохранены.88 portable/vendor result
files:32 cycle CSV +56 benchmark JSON.88 synthesis archives/347 raw hashes.

RESET сохраняет RF/PSW и CPU FRAM, очищает pending peripheral IRQ; новый
KW11 tick после RESET снова будит WAIT. HALT internal fault использует terminal
double-fault policy; abort parity с C для этих случаев не заявлена.
Следующий отдельный gate — EIS. До желательных 1100 LUT осталось 15; полный
board top, banking, native ODT, stack-limit recovery и I/O timeout ещё впереди.
[HALT/RESET](system-control.md), [manifest](verification-cp20.json),
[benchmarks](benchmarks-cp20.json).

## Исторический checkpoint CP19

**CP19 выполнен:** MFPS/MTPS во всех восьми addressing modes. Один kernel
register set, CM=PM=RS=0, NZVC/IPL/T,16-bit addresses, без MMU.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP19a core + probe | 844 | 297 | 4 | 35 MHz PASS, TRACE 36.426 MHz |
| CP19b FRAM/prefetch/IRQ resolver + probe | 1073 | 414 | 4 | 29.56 MHz PASS, TRACE 30.046 MHz |

507 words v10, +14; сохранены все 493 прежних words/labels и весь RTL кроме
opcode decoder. FF/EBR обоих scopes неизменны; относительно CP18 +1/+11 LUT.
170137 completed DCJ11 cases (157285 прежних +12852 новых), отдельно 33788
fault frames (32780 прежних +1008 новых) на каждой RAM/FRAM×portable/vendor ROM.
204 новых normal/trace/IRQ candidates явно исключены по actual abort/I/O/stack
status, без заявления совместимости этих случаев. Новая fault группа без exclusions.
786 benchmarks/ROM; все 766 прежних counts,13 fixtures и 26 cycle CSV сохранены.
Всего 84 synthesis archives/331 raw report hashes проверены.82 portable/vendor
result files совпали побайтно:30 cycle CSV и 52 benchmark JSON.

MFPS Rn — 2 clocks с ideal FETCH, MTPS Rn — 8. FRAM/prefetch скрывает разницу
в новых register loops (40.15625 CPI). MTPS immediate пока использует общий
byte data READ; его отдельный stream fast path не добавлялся.

Следующие gates — HALT/RESET и нужные system operations, затем EIS. Banking,
stack-limit recovery, I/O timeout и полный board top ещё не реализованы.
50 MHz остаётся целью; запас 207 LUT относится только к измеренному FRAM probe.
[MFPS/MTPS](psw-transfer.md), [manifest](verification-cp19.json), [benchmarks](benchmarks-cp19.json).

## Исторический checkpoint CP18

**CP18 выполнен:** все32 CC/NOP encodings и MFPT. Один kernel register set,
CM=PM=RS=0, NZVC/IPL/T, 16-bit addresses, без MMU.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP18e core + probe | 843 | 297 | 4 | 35 MHz PASS, TRACE35.674 MHz |
| CP18f FRAM/prefetch/IRQ resolver + probe | 1062 | 414 | 4 | 29.56 MHz PASS, TRACE30.409 MHz |

493 words v10; прежние452 words/labels и весь RTL кроме decoder сохранены.
157285 completed DCJ11 cases (136165 CP17 +21120 новых, без новых exclusions),
отдельно32780 fault-frame cases на каждой RAM/FRAM×portable/vendor ROM.
766 benchmarks/ROM, все746 прежних counts неизменны. Все три fit-варианта
сохранены; CP18d отклонён по FRAM area1134 LUT.82 archives/323 raw hashes.

Следующие gates — MFPS/MTPS, HALT/RESET, затем EIS. Banking, stack-limit
recovery, I/O timeout и полный board top ещё не реализованы.
[CC/NOP/MFPT](system-flags.md), [manifest](verification-cp18.json).

## Исторический checkpoint CP17

**CP17 выполнен:** trace и RTT, один kernel register set, CM=PM=RS=0,
NZVC/IPL/T, 16-bit addresses, без MMU.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP17a core + probe | 826 | 297 | 4 | 35 MHz PASS, TRACE 36.302 MHz |
| CP17b FRAM/prefetch/IRQ resolver + probe | 1046 | 414 | 4 | 29.56 MHz PASS, TRACE 30.194 MHz |

452 words v10, +1 state FF; сохранены RF/Q/ALU и FRAM transport.
136165 completed DCJ11 cases (124969 CP16 + 11196 trace/RTT), отдельно
32780 CP16 fault-frame cases на каждой RAM/FRAM × portable/vendor ROM.
24 новых FRAM-system cases и 140 trace/fault cases. 746 benchmarks/ROM,
все 726 прежних counts неизменны. 76 synthesis archives/299 raw hashes.

Ещё предстоят HALT/RESET, остальные PSW/system operations, EIS, banking,
red/yellow stack limits, I/O timeout и полный board top. 50 MHz пока не достигнуты.
[Trace/RTT и ограничения](trace-rtt.md), [manifest](verification-cp17.json).

## Исторический checkpoint CP16

**CP16 выполнен:** memory bus/address errors вызывают vector004, fault autoincrement
восстанавливается без штрафа успешной инструкции, ошибка внутри frame — terminal STOP.
Сохранены основной integer subset Stage 1, все 8 addressing modes, byte/word,
IRQ/WAIT/SPL, KW11/KL11 resolver, software/reserved traps, RTI и SPI FRAM prefetch.
Один kernel register set, CM=PM=RS=T=0. MMU отсутствует полностью.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP16f core + FRAM/prefetch + IRQ adapter + probe | **1042** | **413** | **4** | **29.56 MHz PASS**, Fmax 31.117 MHz |
| CP16e core + generic IRQ + probe | **809** | **296** | **4** | **35 MHz PASS**, Fmax 36.552 MHz |

452 words, encoding v9; RF16×16, Q16, один 16-bit ALU, 1024×36 microstore.
По сравнению с CP15: три state FF, два ROM words, десять изменённых words;
labels и успешные instruction counts сохранены. UART/timer/panel/SD/RK board
fit и внешние pin delays ещё не измерены; FPGA не программировалась.

**Проверено:** 124969 completed DCJ11 instruction cases и 32780 fault-frame
cases на RAM/FRAM × portable/vendor ROM; 32 FRAM-system scenarios и 96 новых
second-fault checks. Все 726 прежних benchmarks и все десять cycle CSV совпали
с CP15. 14 Python methods, 2097152 byte ALU checks, 16384 byte RF checks.
74 synthesis archives/291 raw report hashes проверены; current inputs совпадают с CP16e/f.

**Ещё предстоят:** trace/RTT, HALT/RESET и остальные PSW/system operations,
EIS, J-11 bank/mode exchange, red/yellow stack limits, I/O timeout и полный board top.
Отсутствующая ISA пока получает reserved vector010; это не её реализация.

[Memory fault profile и oracle limits](memory-faults.md), [manifest](verification-cp16.json),
[benchmarks](benchmarks-cp16.json), [synthesis](synthesis.md).

## Исторические проверки CP15

124969 completed DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
все 120809 CP14 и 4160 reserved/invalid-mode cases. Из 18536 новых кандидатов
14376 с другим поведением reference исключены явно; они не считаются
совместимыми инструкциями. 96 новых directed frame cases. Все 446 CP14 words,
labels, девять fixtures и per-case cycles неизменны.

726 benchmark runs на ROM-модель; все 714 прежних counts сохранены.
Exhaustive RTL miter подтвердил эквивалентность decoder финального варианта
и базового CP15a для всех 65536 opcodes. Проверены 68 source archives,
267 raw report hashes; current fit inputs совпадают с CP15e/f.

[Semantics и exclusions](reserved-traps.md), [manifest](verification-cp15.json),
[benchmarks](benchmarks-cp15.json), [portable](../tb/reports/cp15-tests.log),
[vendor ISA](../tb/reports/cp15-vendor-isa.log).

## Исторические проверки CP14

120809 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
все 114155 CP13 и 6654 новых IRQ/WAIT/SPL cases без exclusions.
104 directed IRQ cases, 763 adapter checks, пять actual legacy peripheral
scenarios с шестью проверками установившегося состояния. 13 Python methods;
56268 accepted encodings из 65536. Все 422 CP13 words/labels, восемь fixtures
и per-case cycle CSV неизменны. Reset expectation намеренно исправлено на IPL7.

714 benchmark runs на ROM-модель; все 690 прежних counts сохранены.
Nested IRQ→IRQ→RTI→RTI возвращает стек и PSW. Проверены 62 source archives
и 243 raw report hashes; final fit inputs совпадают с CP14c/d.

[IRQ semantics](interrupts.md), [manifest](verification-cp14.json),
[benchmarks](benchmarks-cp14.json), [portable](../tb/reports/cp14-tests.log),
[vendor ISA](../tb/reports/cp14-vendor-isa.log).

## Исторические проверки CP13

114155 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
все 109515 CP12 и 4640 новых trap/RTI cases без exclusions.
112 directed cases проверяют ACK errors и odd SP. Все 65536 encodings
проверены, 56259 поддержаны. Все 393 CP12 words, labels, семь старых
fixtures и per-case cycles неизменны.

690 benchmark runs на ROM-модель, все 666 прежних counts сохранены.
Проверены 58 source archives и 227 raw report hashes; текущие fit inputs
совпадают с CP13a/b. Исправлена сериализация oracle fixture при наложении
stack patches на opcode: исходный emulator не изменён.

[Trap profile и ограничения](software-traps.md), [manifest](verification-cp13.json),
[benchmarks](benchmarks-cp13.json), [portable](../tb/reports/cp13-tests.log),
[vendor ISA](../tb/reports/cp13-vendor-isa.log).

## Исторические проверки CP12

109515 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
все 104991 CP11 и 4524 SWAB/SXT/MARK. 36 новых directed cases, 55744
accepted decoder encodings из 65536 проверенных. Все 355 прежних words,
label addresses, fixtures и cycle CSV остались неизменными.

666 benchmark runs на ROM-модель, все 638 прежних counts сохранены.
Portable/vendor результаты совпадают. Проверены 56 source archives и
219 raw report hashes; текущие synthesis inputs совпадают с CP12c/d.
Новый priority-decoder fit 1081 LUT отвергнут по area, parallel masks дали
984 LUT. Оба варианта и все исходные отчёты сохранены.

[Microcode, tests и exclusions](extra-instructions.md),
[manifest](verification-cp12.json), [benchmarks](benchmarks-cp12.json),
[portable](../tb/reports/cp12-tests.log), [vendor ISA](../tb/reports/cp12-vendor-isa.log).

## Исторические проверки CP11

104991 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
все 90177 CP10 и 14814 новых JMP/JSR/RTS/SOB. 58 directed control tests;
65536 decoder encodings (55552 accepted), 12 Python methods и 4324
sequencer checks. Все прежние primitives, byte/word flags и peripheral tests проходят.

638 benchmark runs на ROM-модель: все 606 CP10 без изменения counts и
32 новых control/stack/program runs. Portable/vendor JSON и per-case CSV
совпадают. На момент CP11 recorder проверил 52 source archives, 203 raw report hashes
и совпадение fit inputs с CP11e/f. Документы и исходные reports
сохраняются отдельно для каждого checkpoint.

Control oracle дополнительно проверяет сам факт vector entry: yellow-stack
trap может очистить fTrap внутри core_step. 456 из 15270 кандидатов исключены
явно (448 trap, 402 I/O, overlap 394). Исходный emulator не изменён.
[Control ISA](control-flow.md), [manifest](verification-cp11.json),
[benchmarks](benchmarks-cp11.json), [portable](../tb/reports/cp11-tests.log),
[vendor ISA](../tb/reports/cp11-vendor-isa.log).

## Исторические проверки CP10

90177 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
12928 RR/BR + 31671 word EA + 7080 word unary + 7440 branch + 23954 byte EA
+ 7104 byte unary. Старые fixtures и per-case cycle CSV не изменились.
Byte ALU: 2097152 checks; writeback: 16384; directed byte CSR/faults: 23+46.
525 prefetch beats, 19 legacy peripheral beats, 65536 decoder encodings
(54528 accepted), 11 Python methods и все прежние primitive tests проходят.

606 benchmark runs на каждую ROM-модель, в том числе все 430 CP9 без
изменения clocks/bus/SPI counts. Portable/vendor результаты совпадают.
На момент CP10 recorder проверил 46 source archives, 179 raw report hashes
и совпадение RTL/microcode/assembler/ROM с final CP10j/k synthesis inputs.

[Byte ISA и exclusions](byte-instructions.md), [manifest](verification-cp10.json),
[benchmarks](benchmarks-cp10.json), [portable log](../tb/reports/cp10-tests.log),
[vendor ISA](../tb/reports/cp10-vendor-isa.log). Исходный DCJ11 emulator не
изменён; четыре read-only address hooks добавлены только в build copy,
чтобы явно исключать внутренние CSR, обходящие public bus callbacks.

## Исторические проверки CP9

* 59119 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
  12928 RR/BR + 31671 EA + 7080 unary + 7440 branch. Все прежние CP8
  fixtures и EA per-case cycles сохранены без изменений.
* Unary: 7104 кандидата; 24 abort явно исключены. Все 12×8 modes, operand
  edges с 16 NZVC combinations, PC/SP и indexed wrap. 19588 exact bus beats;
  99242 RAM clocks с 0..3 waits, 2035650 FRAM clocks.
* Branch: все 15×16 class/flags combinations и 256 offsets у каждого class;
  7440 cases без exclusions, 7440 bus beats; RAM 35464 clocks,
  FRAM 805504. Полный Cartesian product offsets/flags не заявлен.
* 46 unary directed cases проверяют CSR reads/writes, read/write errors,
  odd-word rejection и request stability с 0..3 waits. CLR не читает конечный
  destination; TST не пишет; PSW фиксируется после успешного WRITE ACK.
* Все 65536 decoder encodings проверены; **33280 поддержанных**. Прежние
  10 Python, 66592 ALU, 4096 pairs, 4323 sequencer checks, 503 prefetch
  beats и 19 actual legacy peripheral beats проходят.
* **430 benchmark runs на ROM-модель:** 154 прежних CP8 без изменения counts
  и 276 новых (69 workloads × 4 memory modes). Все portable/vendor JSON
  и differential cycle CSV должны совпадать побайтно.
* Speculation на conditional branch приостановлена микрокодом после
  выявленных лишних SPI reads. Сохранены unrestricted policy measurements.
  BNE self-loop: 144→108 CPI; countdown: 88.176471→72.352941 CPI.

[Verification manifest](verification-cp9.json), [benchmarks](benchmarks-cp9.json),
[portable log](../tb/reports/cp9-tests.log), [vendor ISA](../tb/reports/cp9-vendor-isa.log).
Compressed fixtures/cycle CSV: `tb/reports/cp9-*.gz`. На момент завершения CP9 RTL/microcode/assembler/ROM hashes совпадали
с его финальными CP9e/f synthesis inputs.
Общие oracle callbacks вынесены в `tb/trace_oracle.h`; исправлена строковая
выборка suite name в общем testbench. Это изменения verification, не core.

## Исторические проверки CP8

* 10 Python test methods; v4 BA encoding, запрет старого DZ, packing/roundtrip,
  controls, stream qualifiers и conflicts. Generated ROM не изменился после
  исправления только banner/docstring assembler с v3 на v4; hashes зафиксированы.
* 66592 независимых ALU result/NZVC checks; RF16, 256 dual reads,
  4096 operand-pair checks, BA SUB/BIC writeback, Q/shift/stall/reset.
* 4323 sequencer checks, 1024 ROM reads/holds и 17 feedback transitions;
  memory/PSW masks, byte lanes и READ/WRITE/MDR с 0..7 waits.
* Все 65536 opcodes: **28928 accepted encodings**. Dynamic stream hint:
  65536 encodings; prefetch policy/stalls/reset и 503 transport beats.
* **12928 RR/BR DCJ11 cases**, включая исходные 6272 без изменения порядка.
  RAM с 0..3 waits: **45248 clocks**, FRAM: **1383296 clocks**.
* **31671 EA DCJ11 cases**, все **7×8×8 mode pairs**, registers/PSW и точные
  **137006 bus beats**. RAM 0..3 waits: **948247 clocks**; FRAM:
  **14739672 clocks**. Из 32298 кандидатов исключены 627: 595 abort и 32 I/O.
* 14 directed cases: семь I/O operations, odd source/pointer, пять store errors
  без фиксации нового PSW или записи в memory. CMP/BIT не пишут destination;
  MOV не читает конечный destination; BIC/BIS/ADD/SUB делают один read + write.
* Integration с настоящей замороженной периферией lsi11-fpga: 19 beats —
  KL11, KW11, panel, SD side effects и неизвестный CSR без FRAM alias.
* RR/EA и memory microprograms проверены с portable ROM и vendor DP8KC;
  per-case cycle CSV совпадают побайтно. Verilator --Wall без предупреждений.
* **154 benchmark runs на каждую ROM-модель:** 27 RR RAM, 27 RR FRAM,
  100 EA (25 workloads × 4 memory modes). Все counts совпадают; все прежние
  CP7 benchmarks дали прежнее число clocks, memory beats и SPI transfers.

Полный regression: `make test`. Vendor: `make vendor-test vendor-engine
vendor-memory-engine vendor-core vendor-fram vendor-ea`, с корректным
`LATTICE_SIM_DIR`. Увеличенный RR suite превысил старый общий timeout testbench;
timeout теперь зависит от числа fixtures. CPU/FRAM протокол для этого не менялся.

[Verification manifest](verification-cp8.json), [benchmarks](benchmarks-cp8.json),
[tests](../tb/reports/cp8-tests.log), [vendor EA](../tb/reports/cp8-vendor-ea.log).
Compressed oracle fixtures и cycle CSV сохранены в `tb/reports/cp8-*.gz`.
Архивы и реальные timing failures сохранены, а не перезаписаны проходящим run.

При abort частичное состояние регистров не заявляется идентичным DCJ11:
source mode2/3 increment выполняется после READ ACK. Architectural trap
frame/vector fetch и restart относятся к Stage 2. Byte ISA появилась позднее, в CP10. Подробности: [word-double-operand.md](word-double-operand.md).

## Следующий gate

Architectural memory bus/address traps с очисткой EA CALL state и terminal
frame-fault guard; затем trace/RTT, HALT и прочие system instructions.
EIS идёт отдельным измеренным gate. У CP15f probe остаются 244 LUT и 3 EBR;
полный core с периферией ещё не синтезирован вместе. Эти ресурсы не
резервируются под MMU. [IRQ и ограничения](interrupts.md).
