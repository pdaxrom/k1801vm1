# 36-bit microinstruction, encoding version 12

CP37 добавляет экспериментальный backend APR_READ/D=APR только в build-копии.
Production v12 и 954 words сохранены; MMU translation ещё не подключена.
[Кодирование, измерения и границы lookup gate](mmu-apr-lookup.md).

## CP57 opt-in extension к v12

Default 954 слова сохраняются бит-в-бит. В `--service-cp57` два ранее
нулевых бита READ/WRITE `[8:7]` задают пространство: ACTIVE=0, GUEST=1,
UPPER=2, LOWER=3. Последние два — физическая RAM без I/O/ROM overlays.
Assembler запрещает special space с byte или stream. Контекст CPC/CPSW
читается обычными UPPER microinstructions, адрес формируется через ALU/T5.

JUMP `[2:1]`: NONE=0, ENTER=1, LEAVE=2, CONFIG=3. Требуются `prefetch=0`
и `init=0`; CONFIG получает ready[1:0] с A-порта RF. Формат INIT периферии
в бите 0 не изменён. Поля защищены от overlap с другими control classes.

ALU `d=STATUS` использует D=ZERO encoding и low bit7=1; допустимы NEXT/FETCH,
запрещены PAGE/FETCH_A1/IMM/trace. Значение: `{8'b0,!fp_ready,5'b0,ready[1:0]}`.
Обычный ZERO остаётся нулём, включая PAGE с установленным bit7 адреса.
Внутреннего 16-bit memory-response mux/ACK для STATUS нет. [ABI CP57](service-bank-cp57.md).

## CP31: возврат к v12

FP11 отложен. Удалены 33 FP words и reset hook, control bit1 снова не занят;
assembler отклоняет `private` и `fp_init`. Полный FIS image **954/1024×36**
побайтно совпадает с CP29. Версия v13 и её контексты остаются историческим
[экспериментом CP30](fp11a.md), в текущую сборку не входят.

**CP27 вводит v12:** pair5 теперь **DQ**, D / Q. Прежнее имя `ZQ` остаётся
alias только при `d=ZERO`; assembler отклоняет сочетание ZQ с ненулевым D.
Другие поля и все 700 старых микрокоманд сохранены. Полный store содержит
**954 слова, 349 меток**: 223 слова FIS и 31 linking JUMP. Linker размещает
`fis.uasm` в свободных участках baseline `m0.uasm`, выдаёт readable
`generated/full.uasm` и placement report; существующий assembler проверяет
полный образ. [FIS, совместимость D/Q и проверки](fis.md).

**CP26 сохраняет encoding v11:** 700 слов, 271 метка. 58 новых слов/16 меток;
все 642 слова/255 меток CP25 сохранены. Новых полей или синтаксиса нет.
[DIV addresses, алгоритм и flags](eis-div.md).


**CP25 сохраняет encoding v11:** 642 слова, 255 именованных меток.
MUL добавляет 45 слов и 10 меток; все 597 слов/245 меток CP24 сохранены.
Нет новых fields, ALU ops, register selectors или assembler syntax.
[Адреса, алгоритм и flags](eis-mul.md).

**CP24 сохраняет encoding v11:** 597 words и 245 именованных меток.
Все 592 прежних слова и 242 метки CP23 сохранены на своих адресах.
XOR добавляет 02c, 02e, 02f, 094, 095, без новых полей или assembler syntax.
`flags=NZV` сохраняет C; memory flags обновляет прежний tail 3b4 после WRITE.
[XOR microcode](eis-xor.md).

**CP23 сохраняет v11 и все 592 слова, 242 метки и ROM images CP22 побайтно.**
Изменён только способ реализации next-address logic, без новых fields или
ограничений microcode. [Формальная проверка и synthesis](area-sequencer.md).

**CP22 сохраняет v11:** 592 words, +41 для ASHC. Все 235 labels сохранены; 549 words неизменны, 019/01a исправляют
порядок чтения ASH. T4:Q —32-bit operand, T0 —count, T6 —sticky V; используются
существующие RFQ_L/R, RS1, CALL/OR_MD/PAGE. [ASHC](eis-ashc.md).

**CP21 сохраняет v11:**551 words, +30 для ASH. Все521 прежних words/labels,
assembler и формат сохранены. Счётчик в T0, значение в T4, sticky V в T6;
используются прежние ALU/CJUMP/CALL/OR_MD/PAGE. [ASH](eis-ash.md).

**v11 введён в CP20:** control JUMP bit0=`init` — один синхронный peripheral
reset pulse на следующий settling word. Выход engine зарегистрирован,
чтобы комбинационный decode не поступал на async reset периферии. Ассемблер допускает поле только у JUMP, проверяет0/1 и требует
prefetch=0 при init=1. Target и остальные поля не меняются; ALU bit0 по-прежнему
означает trace=RETURN либо часть IMM. Engine/core/fram_system выводят init
наружу как peripheral_reset. Control word не пишет datapath и не делает
memory beat.521 words; все507 прежних words/labels побайтно сохранены.
Не смешивать новую ROM с engine без peripheral_reset.
[Новые routine addresses и RESET contract](system-control.md).


**CP19 сохраняет v10:**507 allocated words, +14; все 493 прежних words/labels
сохранены. MFPS/MTPS используют D=PSW/IMM/MDR, byte READ/WRITE, MOV destination,
ALU LOAD и существующий sequencer. Новых fields/context interpretations нет.
[Новые routine addresses](psw-transfer.md).


**CP18 сохраняет v10:** 493 allocated words; все 452 прежние words/labels
сохранены. CC/NOP и MFPT используют только существующие fields.
[Dispatch и microcode](system-flags.md).

**v10 введён в CP17:** ALU `trace=RETURN` кодируется bit0 при seq=FETCH,
flags=KEEP и D≠IMM. RTI использует восстановленный PSW.T, RTT (IR[2]=1)
подавляет trace собственного возврата. Остальные ALU words используют T,
сохранённый при FETCH. D=IMM сохраняет все восемь immediate bits.

037 — единственный изменённый word относительно CP16 (bit0 0→1).
Все 452 words и labels остаются на прежних адресах. Microsequencer получает
trace predicate и направляет его на прежний BPT entry 024 с приоритетом над IRQ.
Не смешивать ROM v10 с прежним engine: RTI T=1/RTT требуют новой policy.


**v9 введён в CP16:** control15=`TRAP`, control READ bit2=`fault_inc`.
TRAP использует target как JUMP и отмечает construction frame для защиты от
second fault. fault_inc выполняет ровно один ADD STEP/TWO continuation при
ошибке READ, затем sequencer выбирает 015. Ассемблер проверяет same-register
write, flags=KEEP, seq=NEXT и запрет Q/PSW side effects. Успешный READ не меняется.
452 words; RTL/ROM/assembler должны обновляться вместе. [Подробности](memory-faults.md).

**v8 введён в CP14:** control14=`WAIT`, 446 words в CP14, 450 в CP15. Остальные 36-bit fields
сохранены; ALU FETCH/FETCH_A1 допускают IRQ redirect на 013 при разрешённом
priority. WAIT удерживает uPC до IRQ, production word задаёт prefetch=0.
Для изменения IPL PSW LOAD предшествует terminal ALU word: IRQ comparator
читает PSW до фронта, без bypass от ALU. [Контракт](interrupts.md).

**v7 введён в CP11:** ALU seq3=`FETCH_A1`, conditional retirement по
исходному RF[A]==1. DISP использует IR[14] как branch/SOB context.
В CP11 355 words, в CP12 393, в CP13 422; прежние 36 bits и четыре EBR. Эксперимент v6 проверял ALU Z;
его ROM/RTL нельзя смешивать с v7, хотя seq field занимает те же bits.
SOB сохраняет PSW и завершается за один или два execution cycles.

**v5 введён в CP10:** dst5=OPERAND, dst6=MOV и control bit3=`byte=IR`.
Byte opcode выбирает ALU ширину только для ALU words с flags=NZV/NZVC
либо dst=OPERAND/MOV. Адресные и scratch операции dst=RF, flags=KEEP
остаются 16-bit. FETCH всегда word. Поля не расширены, 342 words/4 EBR.
RTL v4 не понимает новые destinations/qualifier: пересобирать ROM и RTL вместе.

**v4 введён в CP8, сохранён в CP9:** pair7 — **BA**, то есть read_b / read_a. Это обеспечивает
однократный SUB/BIC с записью в RF[B]. Прежнюю пару DZ удалили; D loads
переведены на PASSA/DA. Старый PASSB/DZ для нуля заменён PASSA/DA с D=ZERO.
Ассемблер отвергает DZ; общего автоматического преобразования нет.
**ROM v3 и datapath v4 нельзя смешивать.** Остальные поля и четыре EBR
сохранены. Ниже также приведена история v1→v3.

Version 2 сохраняет ALU encoding v1, но выравнивает control A/B с ALU A/B.
Это убрало mux перед динамическим выбором RF. CP3r1 → CP3r2:
625 → 610 LUT4, 38.389 → 39.228 MHz при constraint 50 MHz (оба timing fail).
Старые v1 images несовместимы с текущим control decoder; пересобирать
microcode и RTL вместе. Исходные варианты сохранены в synthesis archives.

Version 3 поменял местами command FETCH/CALL. FETCH теперь имеет command=2,
что совпадает с ALU ADD, и в неиспользуемых target/condition bits хранит
обычные `pair=AD, dst=RF, flags=KEEP, d=TWO`. A=B=R7. Нет аппаратных
override mux для operation/pair/D-input. Полный M0 при constraint 50 MHz:
v2 639 LUT4 / 38.806 MHz → v3 582 LUT4 / 42.902 MHz; оба не проходят 50 MHz.
ALU encoding остаётся прежним. Control images v1/v2/v3 нельзя смешивать.

CP6 добавляет обратно совместимый qualifier bit5 для `READ, a=R7, stream=1`.
Старые v3 слова и M0 image не меняются. В CP6 был разрешён только word READ с A=R7;
assembler тогда запрещал byte=1, другой A и stream у WRITE. Память получает
признак instruction stream, READ пишет только MDR. Увеличение PC на 2 —
следующая обычная ALU microinstruction; IR остаётся opcode. FETCH всегда
является stream независимо от bit5. `microcode/stream_test.uasm` проверяет
opcode и три extension words; он не включён в 23 слова M0 ISA microcode.

CP7 расширяет hint на `READ, a=RS/RD, stream=1`: он действует только при
фактическом выборе R7. `uj11_stream` проверяет выбранный register, поэтому
обычный operand `(Rn)+` не запускает instruction prefetch.

CP7 control bit4 (`prefetch=0`) приостанавливает новые speculative reads,
не прерывая demand или sequential READ. По умолчанию `prefetch=1`, bit4=0.
ALU words наследуют policy последнего control word через один FF вне core;
assembler запрещает этот field в ALU words, сохраняя их immediate/page bits.
В EA все control words держат pause, кроме RETURN для register destination;
FETCH возвращает обычный prefetch. Backward compatibility v3 сохранена:
старые reserved-zero control words означают enabled, микрокоманда всё ещё 36 bits.

Слово 36 bits; bit35 выбирает ALU (0) или control (1). У каждого слова
одна строка source; labels, `.org`, `$hex`, `0xhex`, decimal и `; comments`
сохраняют знакомый подход microasm. Backend отдельный: native microcpu
16-bit opcodes с этим форматом бинарно несовместимы.

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
Примитивы и word/byte flags проверены; EIS ещё не реализован в ISA.
NZVC формируются из ALU result **до** RFQ_L/RFQ_R writeback shift;
совместный shift сам не обновляет flags. NOTA устанавливает ALU C=1;
PASS/AND/OR/XOR/BIC дают V=C=0; NZV update сохраняет архитектурный C.
Shifts/rotates дают V=N XOR shifted-out bit, C=shifted-out bit.

Sequencing 0 NEXT (uPC+1), 1 PAGE (`{uPC[9:8],low8}`),
2 FETCH (0x020, одновременно retirement), 3 FETCH_A1 (FETCH+retire при
исходном RF[A]==1, иначе NEXT). Predicate учитывает все 16 bits, не PSW
или ALU output. Stall запрещает и transition, и retirement. Prefetch policy
запрещает speculative launch на seq3; обычные ALU words наследуют policy.
IMM использует low8 только с NEXT/FETCH. PAGE+IMM — ошибка assembler.
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
| 10:7 | 4 | condition |
| 6 | 1 | forced byte transaction (`byte=1`) |
| 5 | 1 | READ stream qualifier; zero for other ordinary control words |
| 4 | 1 | CP7: pause speculative launches (`prefetch=0`) |
| 3 | 1 | operand width from IR (`byte=IR`); READ/WRITE only |
| 2 | 1 | CP16 READ-only `fault_inc`: one validated continuation on fault |
| 1 | 1 | reserved, zero |
| 0 | 1 | JUMP: init (v11); остальные control: zero |

Command 0 JUMP, 1 CJUMP, 2 FETCH, 3 RETURN, 4 DISPATCH,
5 OR_MS, 6 OR_MD, 7 OR_RR, 8 OR_BT, 9 OR_R67,
10 CALL, 11 READ, 12 WRITE, 13 STOP, 14 WAIT, 15 TRAP.
READ/WRITE продолжают на полный target после completion. FETCH — на
opcode entry из слова, принимаемого вместе с IR. Sequencer не декодирует
memory handshake: `advance` удерживает ROM/uPC до ACK; `step` отдельно
запрещает commit при fault. При fault_inc выполняется одна continuation,
затем 015; остальные faults сразу переходят 015. CALL slot очищается.
FETCH — исключение из обычного control layout: bits20:10 имеют ALU layout,
а target и condition не читаются. Assembler задаёт эти поля автоматически
и не разрешает программисту менять FETCH operands. Для остальных control
words ALU может вычислять произвольный результат, но RF/Q/PSW write выключен.

Conditions 0 ALWAYS, 1 C, 2 V, 3 Z, 4 N, 5 Q0, 6 LOOPZ, 7 ERROR;
8..15 инвертируют соответствующий predicate. Условие читает старые flags.
Q0/LOOPZ/ERROR — входы sequencer primitive; loop counter пока не включён в core.

OR masks: MS/MD 0x007, RR/BT 0x003, R67 0x001. Base должен быть выровнен
по mask+1; получаемые адреса должны принадлежать заполненным source words.
RR=`{dst_mode==0,src_mode==0}`; BT=`{address_odd,byte_instruction}`;
R67=`~(selected_A[2]&selected_A[1])`. Никакого hidden addition.

CALL записывает uPC+1 в единственный link register, RETURN его читает.
Аппаратного stack depth>1 в первом gate нет. Nested CALL и RETURN без CALL
направляют sequencer на 0x3ff; link_valid защищает сохранённый return address.
Оба случая проверяются unit tests. Ассемблер проверяет статические targets
и return continuation, но пока не доказывает максимальную динамическую
глубину вызовов через opcode dispatch.

Все неиспользуемые ROM locations заполняются STOP. Occupancy считается по
явно собранным словам, включая intentional dispatch stubs, отдельно от
1024 физических строк. `listing`, JSON labels/stats и EBR INITVAL генерируются
из одного assembled image. Hand-written hex ROM не используется.

## CP9: формат сохранён

Encoding v4 не изменён. 342 words включают unary register/memory routines
и все branch conditions, составленные из CJUMP N/Z/V/C и инверсий. На
branch CJUMP задано prefetch=0: ALU continuation наследует pause, следующий
FETCH возвращает разрешение. Никаких новых fields/conditions не потребовалось.


## CP10: operand width и общий microcode

`byte=IR` и `byte=1` взаимоисключающие. Final operand READ/WRITE применяют
`byte=IR`, pointer/displacement reads — word. `stream=1` разрешён с
`byte=0/IR` при A=R7/RS/RD; actual R7 qualification остаётся аппаратной.
При byte immediate core получает low byte; FRAM читает целое stream word
и сохраняет шаг prefetch +2. Forced byte с stream запрещён assembler.

Byte classification: `IR[15] && IR[14:12]!=6 && |IR[14:11]`. Она отличает
поддержанные byte classes от SUB и branches. При добавлении будущих
system/EIS instructions этот predicate нужно перепроверить. Unsupported
opcodes с CP15 входят в reserved trap до operand execution.


## CP28 board ROMs

36-bit v12 microinstruction и все 954 microcode words CP27 сохранены.
Board dispatch использует отдельный 1024×9 EBR; это таблица entry addresses,
не расширение microinstruction. Все текущие entries меньше512, диапазон
проверяется генератором. Firmware512×16 использует ещё один EBR через два
byte ports. Итого полный board top использует6 EBR, microstore по-прежнему4.
[Адресная компрессия, FETCH clock и проверки](hc1200-integration.md).
