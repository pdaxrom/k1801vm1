# uJ11: архитектура CP26

uJ11 — специализированный микрокодный PDP-11/J-11 integer execution engine
для LCMXO2-1200HC. Все адреса 16-битные, logical == physical. MMU отсутствует.
CP26 реализует 7 word/5 byte double-operand и 12 word/12 byte unary classes во всех восьми
addressing modes, все 15 branch classes и JMP/JSR/RTS/SOB/SWAB/SXT/MARK, BPT/IOT/EMT/TRAP, RTI/RTT, trace, WAIT, SPL, CC/NOP/MFPT, MFPS/MTPS, HALT restart/RESET, ASH/ASHC/XOR/MUL/DIV, resolved IRQ, reserved/invalid-mode traps и memory vector004. Запуск ОС, привилегии и полный instruction set пока не заявлены.

Ограничения: целевой полный core ~900–1000 LUT, желательно <1100 из 1280;
7 EBR всего, начальная цель clock 50 MHz. Пройденные gates: ROM + sequencer,
RF/Q/ALU, integrated datapath, decoder, минимальная ISA, FRAM и word EA.
CP26 core: 867 LUT4/299 FF/4 EBR, 35 MHz PASS, TRACE 37.151 MHz.
Core+FRAM+KW11/KL11 IRQ adapter: 1099/416/4, TRACE 31.771 MHz,
29.56 MHz PASS. Counts включают probe; исходные 50 MHz не достигнуты.
Все варианты и фактические critical paths — в [synthesis.md](synthesis.md).

## Datapath

* Один 16-bit ALU, asynchronous dual-read RF 16×16, write port по B.
  R0–R7 архитектурные, T0–T7 временные. Q16 отдельно.
* Selectors 0–15 literal; RS/RD и RS|1/RD|1 динамические из IR.
* Один writeback; ALU operation и source selection независимы.
* Отдельный PSW16, IR16, MDR16; ALU flags N/Z/V/C, C при subtraction — borrow.
* FETCH читает слово по R7, ALU увеличивает R7 на 2, IR принимает opcode.
  Decode входящего слова выбирает execution microinstruction на том же
  фронте synchronous microstore. Дополнительного state decode не нужно.
* PC хранится только в RF[R7]. FETCH использует обычный A read port
  с A=B=R7. Третий RF port и PC mirror не нужны и не реализованы.

АЛУ MVP: PASS A/B, ADD/ADC, SUB/SBC (A−B), AND/OR/XOR/BIC, NOT A,
one-bit LSL/LSR/ASR/ROL/ROR. INC/DEC/NEG/reverse SUB выражаются source
selection и тем же adder. В v4 pair BA подаёт read_b/read_a при записи
по B; это даёт один execution cycle для PDP-11 SUB/BIC. Q load и совместные RF/Q shifts — отдельные
destination controls. Не добавляются multiplier, divider и barrel shifter.
Byte ALU использует тот же adder, выбирая bit7/15 для flags; OPERAND/MOV
writeback обеспечивает сохранение high byte либо MOVB sign extension.

## Microstore и sequencing

1024×36, четыре явных 1024×9 DP8KC, один фронт на микрокоманду.
Регистр выхода ROM служит uIR. Sequencer вычисляет **следующий** адрес
из текущего слова; при stall и ROM, и архитектурные side effects заморожены.
Reset принудительно читает entry 0, затем microcode инициализирует RF.

ALU word содержит common operation/enables и 8-bit next within page;
control word перекрывает datapath fields для полного 10-bit target,
condition и OR dispatch. Это не сохранение hardwired state enum в ROM.
В encoding v3 A/B fields выровнены между форматами, а FETCH переиспользует
ALU fields для ADD R7,2; это устранило измеренно дорогие override mux.
CALL/RETURN: один return register (не рекурсивный стек). Вложенные вызовы
потребуют отдельного измеренного изменения. Double-operand path использует три последовательных
CALL к общим source EA, destination EA и register-source capture routines;
вложенных вызовов нет.

Fixed FETCH entry 0x020, reset page 0x000, RR execution entries
0x110 MOV, 0x120 CMP, 0x130 BIT, 0x140 BIC, 0x150 BIS, 0x160 ADD,
0x1e0 SUB, 0x044 BR (общий taken tail 0x180), diagnostic STOP 0x3ff.
Unary entries 0a0..0ce, memory tails 0d0..101; branch entries 044..07c,
общие tails 18f..193. Unary CALL использует прежний destination EA.
CP7 занимает source page 200..261, destination 280..2c9 и execution tables
300..3d5. JMP/JSR/RTS/SOB entries 080/088/090/098. CP12 entries 1a0/1b0/1b4/1c0/1c8 и SWAB tail 1d0/2d0.
CP13 добавляет software entries 024/026/028/02a, RTI 031..037 и trap frame 3d6..3e3.
CP14 WAIT/IRQ entries 012/013, SPL 1f0..1ff/3e4..3e8.
CP15 invalid JMP/reserved entries 040/042.
CP16 BUS_FAULT_ENTRY — 015/016, control15=TRAP, READ bit2=fault_inc.
CP17 использует прежний BPT entry 024 для trace; RTI/RTT делят 031..037.
CP22 содержит ASH/ASHC:592 assembled words. Счётчик размещён в существующем T0;
нового RTL state нет. CP24 добавляет XOR: всего 597 assembled words.
XOR entries 02c/02e и tail 094/095 переиспользуют ALU XOR и destination EA;
source register читается после EA/read. CP25 MUL добавляет 45 слов: всего 642. ALU/RF/Q и формат прежние;
[16-step multiply](eis-mul.md) выполняется микрокодом.
Остальные ROM words не резервируются под MMU.
Control dispatch OR_MS/MD/RR/BT/R67 задаёт entry OR IR predicates.
Assembler проверяет, что entry не маскирует выбираемые dispatch bits.

Decoder использует opcode high nibble 1..6/9..D/E для double operands;
unary IR[11:6] и branch {IR[15],IR[10:8]} непосредственно формируют entry.
Byte classes 9..D используют те же entries, что word 1..5. Если хотя бы один mode
не нулевой, bit3 entry выбирает соседний общий EA path (118/128/138/148/158/168/1e8).
JMP/JSR/RTS/SOB и SWAB/SXT/MARK получают компактные entries.
Непересекающиеся classes объединены parallel masks; все 65536 opcodes проверены. Остальное ведёт на vector010; invalid JMP mode0 — на 004. Это политика
отсутствующей ISA, не её реализация. Diagnostic STOP остаётся для повторной memory error внутри frame
и microengine errors; первая ошибка памяти вызывает vector004.
STOP — microengine fault, **не реализация PDP-11 HALT или trap vector 010**.
PLM/distributed-ROM alternatives пока не измерялись. В CP9f путь через
I/O read-data selection и opcode dispatch стал critical; будущую оптимизацию
нужно измерять в полном FETCH path.

## Memory и prefetch

Одна транзакция: `addr[15:0], request, write, byte, write_data[15:0]`,
ответ `ack, read_data[15:0], error`. Read обозначается `request && !write`.
Completion происходит на фронте `request && ack`; error значим при ack.
Всю транзакцию address/control/data стабильны. Байт имеет точный byte
address и payload в write_data[7:0]. Word по нечётному адресу вызывает vector004
до внешнего request. Failed ACK не фиксирует результат операции.
Память может отвечать с нулём или несколькими wait clocks. Wishbone отдельно.

CP6 добавил MR45V100A SPI FRAM, сохранение READ между последовательными
словами и однословный instruction-stream prefetch. CP7 добавляет микрокодные
EA и launch policy, CP9 приостанавливает speculation на branch predicates.
PF_DATA физически использует уже имеющийся transport rdata16; PF_VALID и
буфер ошибки добавляют два FF к варианту без prefetch. Tag хранит 15 бит
адреса следующего выровненного слова, это не зеркало PC и не третий RF port.
После FETCH либо `READ, a=R7/RS/RD, stream=1` с фактически выбранным R7
предсказывается следующее слово. Control bit4 (`prefetch=0`) удерживает
запуск speculation на участках EA с предстоящим operand access. Последний
policy наследуется ALU words через один FF.
READ extension пишет MDR, сохраняя IR; PC продвигается только микрокодом.
Запись PC сохраняет prediction лишь при совпадении адреса. Изменение PC,
data access и write сбрасывают несовпадающий буфер; незаконченное speculative
чтение заканчивает слово и отбрасывается. По заполнении буфера SCK стоит.
CP10 регистрирует результат PC mismatch в отдельном FF. Он маскирует
prediction/buffer valid сразу после PC-write edge; следующий clock очищает
хранимые valid bits. Это убирает цепочку valid control после ALU из critical
path без bubble. Совпадающий PC сохраняет buffer. Byte immediate потребляет
полное выровненное FRAM stream word, отдавая core low byte.
I/O page 160000..177777 octal обслуживается только demand-запросами.
Тесты проверяют tags, redirects, SMC, byte lanes, odd words, reset и I/O.

Два такта RR path относятся к ideal RAM. В RTL simulation SPI-протокола получено
107 clocks/instruction у прежнего контроллера, 39,078125 с sequential READ
и prefetch в RR loop. CP22 IRQ adapter probe: 1098 LUT4 / 416 FF / 4 EBR,
29,56 MHz PASS. Периферия/boot/RK не входят в этот synthesis scope.
Подробности: [fram-peripherals.md](fram-peripherals.md).

## J-11 banking и этапы

J-11 имеет alternate R0–R5, K/S/U stack pointers, PSW mode и register-set
bits независимо от memory translation. Их нельзя считать реализованными
только потому, что PSW физически 16-битный. CP22 поддерживает один kernel
набор, CM=PM=RS=0; trap/RTI/RTT сохраняют и загружают NZVC/IPL/T.
Вне этого профиля совместимость не заявляется.
Для полной J-11 mode/register-set совместимости понадобится microcoded обмен
активного набора с backing storage. Первая trap/RTI версия может документированно
ограничиться одним kernel register set. Не увеличивать быстрый RF ради него.

CP10 добавил byte operations, CP11 — JMP/JSR/RTS/SOB, CP12 — SWAB/SXT/MARK.
CP13 начал Stage 2: [software traps и RTI](software-traps.md). CP14 добавляет [IRQ/WAIT/SPL](interrupts.md). CP15 добавляет [reserved traps](reserved-traps.md). CP16 добавляет [memory bus/address traps](memory-faults.md). CP17 добавляет [trace/RTT](trace-rtt.md), CP18 — [CC/NOP/MFPT](system-flags.md). CP19 добавляет [MFPS/MTPS](psw-transfer.md), CP20 — [HALT restart/RESET](system-control.md). 
CP21 начал Stage 3 с [ASH через существующий RF/ALU](eis-ash.md).
Остальные EIS — отдельные gates через Q/ALU/small counter, Stage 4 — нужные system instructions,
Stage 5 — измеренная оптимизация. MMU не входит ни в один из этих этапов.

Точные ограничения и текущий gate — [implementation-status.md](implementation-status.md).


## IRQ boundary

Generic resolved IRQ принимается по PSW IPL только при ALU retirement либо
WAIT, с однократным ACK. Vector временно использует MDR, затем общий CP13
frame; RF не расширяется. WAIT и IRQ accounting добавляют три state FF.
SPL и RTI меняют IPL до terminal ALU word; обход ALU→IRQ priority отсутствует.
KW11 pulse latch + UART resolver находятся отдельно от core. Boot PSW=0340.
Details, scope и tests: [interrupts.md](interrupts.md).

## CC/NOP/MFPT

CP18 добавляет 41 allocated words без изменения 36-bit fields или state.
Direct IR mask dispatch в свободные page1 slots и существующий OR_MD
позволяют выполнить CC за 4 CPI ideal RAM; MFPT за 2.
[Microcode и измеренное сравнение трёх размещений](system-flags.md).

## MFPS/MTPS

CP19 сохраняет v10, добавляет 14 words и только decode terms. MFPS использует
D=PSW и прежний MOVB sign extension, NZV/C policy и byte WRITE. MTPS маскирует
operand/old PSW через обычный ALU/T2/T4 и сохраняет T. PSW LOAD предшествует
terminal FETCH, чтобы resolved IRQ видел новый IPL. Нет нового PSW bypass
или register file port. [Семантика, тесты и цены](psw-transfer.md).

## CP20: HALT restart / peripheral RESET

521 words v11, RF/Q/ALU сохранены; добавлен один FF выхода RESET.
Control JUMP bit0=init фиксирует peripheral_reset на следующий clock
(settling ALU); terminal ALU проверяет IRQ после снятия импульса.
Сигнал идёт в reset периферии и IRQ pending latch, но не в reset CPU/FRAM.
HALT использует TRAP frame guard, сохраняет PSW/PC, читает004, очищает PC bit0,
загружает PSW0340. Это профиль нашего DCJ11 emulator, не полный native ODT.
Старые507 microinstructions и весь datapath/sequencer сохранены.
[Контракт, проверка actual peripherals и ограничения](system-control.md).

## CP21: первый EIS gate

ASH использует существующие T0/T4/T6, one-bit shifts и PSW flags.
Microstore551 words v11; +9 LUT в FRAM scope, без новых FF. Все прежние words
сохранены. Формулы latency, flags/alias/fault проверки и пределы fit —
в [eis-ash.md](eis-ash.md). Исторический результат:1094 LUT. В CP22 исправлен порядок ASH EA;
текущий FRAM scope1098 LUT. [Исправления](eis-ashc.md).

## CP22: ASHC и исправления ASH

[ASHC](eis-ashc.md) использует T4:Q и five-cycle shifts. Count EA выполняется
до чтения целевого регистра/пары; flags относятся к32-bit результату,
независимо от odd-register alias stores. Исправленный C executor и независимый
bit-by-bit reference проверяются отдельно. Новых state registers нет.

CP26 DIV: ещё 58 слов (всего 700), общий MUL/DIV direct-bit dispatch,
16-step restoring loop через прежние ALU/Q/RF. Even R по DEC; odd R —
явный SIMH-style software profile. [Реализация и ограничения](eis-div.md).

CP27 FIS: FADD/FSUB/FMUL/FDIV используют память как floating-point stack,
T0…T7/Q как рабочее состояние и прежний ALU. Pair5 расширен до D/Q;
никаких floating accumulators, новых RF ports, FF, multiplier/divider или
EBR не добавлено. Exact rounded F-format и exception profile описаны
отдельно от исторического KE11-F guard-bit поведения. Полный store —
954/1024×36 v12. [FIS](fis.md), [полный board top как следующий gate](hc1200-integration.md).
