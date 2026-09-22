# Формат микрокода uJ11

Текущий формат: 36 бит, v12 с расширениями USER/HALT и STEP.
Microstore 1024×36, занято 1005 слов. Единственный рабочий исходник —
[uj11.uasm](../microcode/uj11.uasm), assembler — [uj11asm.py](../microasm/uj11asm.py).
`make hardware` создаёт ROM, listing, labels и stats в `build/hardware`.
Все неуказанные адреса заполняются STOP; отдельного старого m0/FIS linker нет.

Полный синтаксис, значения полей по умолчанию, CLI, собираемые примеры
и диагностика — в [руководстве по микроассемблеру](microassembler.md).

Синтаксис: одна микрокоманда на строку, метки, `.org`, `$hex`, `0xhex`,
`0o` для восьмеричных, обычные числа десятичные, комментарии после `;`.

## ALU word

| Bits | Width | Поле |
|---|---:|---|
| 35 | 1 | 0 |
| 34:31 | 4 | ALU operation |
| 30:26 | 5 | A selector |
| 25:21 | 5 | B selector, также RF write address |
| 20:18 | 3 | operand pair |
| 17:15 | 3 | destination / Q shift |
| 14:13 | 2 | PSW update |
| 12:10 | 3 | D input |
| 9:8 | 2 | sequencing |
| 7:0 | 8 | page next или unsigned immediate8 |

ALU op 0..15: PASSA, PASSB, ADD, ADC, SUB, SBC, AND, OR, XOR, BIC,
NOTA, LSL, LSR, ASR, ROL, ROR. SUB/SBC вычисляют A−B[−C], C=borrow.
Flags 0 KEEP, 1 NZV (preserve C), 2 NZVC, 3 LOAD (PSW←result).
Для word MOV используется PASSA/NZV, для CMP SUB/NZVC без записи.

A/B: 0..7 R0..R7, 8..15 T0..T7, 16 RS, 17 RD, 18 RS1, 19 RD1;
20..31 запрещены. RS1/RD1 означают IR register index **OR 1**, не +1.
Pairs 0 AB, 1 AQ, 2 AD, 3 DB, 4 ZB, 5 DQ (ZQ при D=ZERO), 6 DA, 7 BA.
D 0 ZERO, 1 ONE, 2 TWO, 3 STEP, 4 MDR, 5 DISP, 6 IMM, 7 PSW.
DISP при IR[14]=0: `sign_extend(IR[7:0])<<1` (branches); при IR[14]=1:
`zero_extend(IR[5:0])<<1` (SOB). MARK использует первый context:
его IR[7:6]=0, поэтому получается требуемый unsigned six-bit offset×2.
STEP=1 для byte R0–R5, иначе 2.
STEP используется в modes 2/4; deferred modes 3/5 и PC displacement advance
всегда используют TWO. Это маленькая константа, а не отдельный EA engine.

Destination 0 NONE, 1 RF, 2 Q, 3 RFQ_L, 4 RFQ_R, 5 OPERAND, 6 MOV; 7 reserved.
OPERAND при byte сохраняет RF[B][15:8], MOV знаково расширяет result[7:0].
При word оба пишут полный result. RF writeback, включая PC, содержит уже
объединённое значение; flags используют исходный ALU result нужной ширины.
RFQ_L: RF[B]←{result[14:0],Q[15]}, Q←{Q[14:0],0}.
RFQ_R: RF[B]←{result[15],result[15:1]}, Q←{result[0],Q[15:1]}.
EIS и FIS используют эти примитивы в действующем микрокоде.
NZVC формируются из ALU result **до** RFQ_L/RFQ_R writeback shift;
совместный shift сам не обновляет flags. NOTA устанавливает ALU C=1;
PASS/AND/OR/XOR/BIC дают V=C=0; NZV update сохраняет архитектурный C.
Shifts/rotates дают V=N XOR shifted-out bit, C=shifted-out bit.

Sequencing 0 NEXT (uPC+1), 1 PAGE (`{uPC[9:8],low8}`),
2 FETCH (0x020, одновременно retirement), 3 FETCH_A1 (FETCH+retire при
исходном RF[A]==1, иначе NEXT). Predicate учитывает все 16 bits, не PSW
или ALU output. Stall запрещает и transition, и retirement. В текущей
плате speculative prefetch отсутствует; seq3 не включает его.
IMM использует low8 с NEXT/FETCH/FETCH_A1. PAGE+IMM — ошибка assembler.
Без IMM/PAGE low8 должен быть нулём. Dsel != IMM с заданным literal запрещён.

Пример ровно одной execution microinstruction:

```
.org $160
ADD_RR:
    alu ADD, a=RS, b=RD, pair=AB, dst=RF, flags=NZVC, seq=FETCH
```

## Control word

| Bits | Width | Поле |
|---|---:|---|
| 35 | 1 | 1 |
| 34:31 | 4 | command |
| 30:26 | 5 | A selector, совпадает с ALU |
| 25:21 | 5 | B selector, совпадает с ALU |
| 20:11 | 10 | full target |
| 10:7 | 4 | CJUMP: condition; READ/WRITE bits8:7: memory space |
| 6 | 1 | forced byte transaction (`byte=1`) |
| 5 | 1 | READ stream qualifier; zero for other ordinary control words |
| 4 | 1 | pause speculative launches (`prefetch=0`) |
| 3 | 1 | operand width from IR (`byte=IR`); READ/WRITE only |
| 2 | 1 | READ-only `fault_inc`: one validated continuation on fault |
| 2:1 | 2 | JUMP: service NONE/ENTER/LEAVE/CONFIG; в READ bit2 — fault_inc |
| 0 | 1 | JUMP: init; остальные control: zero |

Command 0 JUMP, 1 CJUMP, 2 FETCH, 3 RETURN, 4 DISPATCH,
5 OR_MS, 6 OR_MD, 7 OR_RR, 8 OR_BT, 9 OR_R67,
10 CALL, 11 READ, 12 WRITE, 13 STOP, 14 WAIT, 15 TRAP.
READ/WRITE продолжают на полный target после completion. FETCH — на
opcode entry из слова, принимаемого вместе с IR. Sequencer не декодирует
memory handshake: `advance` удерживает ROM/uPC до ACK; `step` отдельно
запрещает commit при fault. При fault_inc выполняется одна continuation,
затем fault entry: 0x015 в USER или 0x2b6 в HALT; остальные faults сразу
переходят к этому entry. При redirect CALL slot очищается. Fault во время
защищённого построения frame останавливает CPU.
FETCH — исключение из обычного control layout: bits20:10 имеют ALU layout,
а target и condition не читаются. Assembler задаёт эти поля автоматически
и не разрешает программисту менять FETCH operands. Для остальных control
words ALU может вычислять произвольный результат, но RF/Q/PSW write выключен.

Conditions 0 ALWAYS, 1 C, 2 V, 3 Z, 4 N, 5 Q0, 6 LOOPZ, 7 ERROR;
8..15 инвертируют соответствующий predicate. Условие читает старые flags.
Q0/LOOPZ/ERROR — входы sequencer primitive. В production engine Q0=Q[0],
LOOPZ=ERROR=0; ошибки обслуживаются отдельным fault redirect. Отдельного
аппаратного loop counter нет, циклы используют RF.

OR masks: MS/MD 0x007, RR/BT 0x003, R67 0x001. Base должен быть выровнен
по mask+1; получаемые адреса должны принадлежать заполненным source words.
RR=`{dst_mode==0,src_mode==0}`; BT=`{address_odd,byte_instruction}`;
R67=`~(selected_A[2]&selected_A[1])`. Никакого hidden addition.
OR_BT не допускает `a=` в синтаксисе: A=R0, address_odd=R0[0].
OR_R67 допускает выбор A; для архитектурных R6/R7 его bit0=0, для R0..R5 — 1.

CALL записывает uPC+1 в единственный link register, RETURN его читает.
Аппаратного стека глубже одного вызова нет. Nested CALL и RETURN без CALL
направляют sequencer на 0x3ff; link_valid защищает сохранённый return address.
Оба случая проверяются unit tests. Ассемблер проверяет статические targets
и return continuation, но пока не доказывает максимальную динамическую
глубину вызовов через opcode dispatch.

Все неиспользуемые ROM locations заполняются STOP. Occupancy считается по
явно собранным словам, включая intentional dispatch stubs, отдельно от
1024 физических строк. `listing`, JSON labels/stats и EBR INITVAL генерируются
из одного assembled image. Hand-written hex ROM не используется.


## Контекстные поля и служебное пространство

READ/WRITE bits8:7: ACTIVE=0, GUEST=1, UPPER=2, LOWER=3. UPPER/LOWER —
физическая FRAM без I/O/ROM overlays. Ассемблер запрещает сочетать special
space с byte или stream. CPC/CPSW читаются через эти же обычные операции.

JUMP bits2:1: NONE=0, ENTER=1, LEAVE=2, CONFIG=3. Для ненулевого service
требуются `prefetch=0` и `init=0`; CONFIG получает ready[1:0] и debug bits
с A-порта RF. Полная таблица CONFIG/STATUS — в [руководстве](microassembler.md).
ALU `d=STATUS` использует D=ZERO и bit7=1, только с NEXT/FETCH;
PAGE, FETCH_A1, IMM и trace с ним несовместимы.
ALU `trace=RETURN` использует low bit0=1, только с FETCH, KEEP и без IMM.
Это завершение RTI/RTT с корректной обработкой trace.

`byte=IR` и `byte=1` взаимоисключающие. READ stream допустим только с
A=R7/RS/RD и byte=0/IR. Пометка сохраняется в кодировке, но production
engine её не использует. Pointer/displacement reads всегда word.
Byte opcode определяется отдельно от SUB/branch.

Физическая плата не включает speculative prefetch. Наличие полей
prefetch/stream в формате не означает, что отдельный prefetch engine собран.
[Состав CPU и EBR](architecture.md), [история формата](../history/README.md).
