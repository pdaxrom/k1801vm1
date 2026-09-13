# Микроархитектура: M0 → CP27

> История datapath и его изменений. Для установленной CP67b см.
> [microstore/decode, USER/HALT и загрузку](system-cp67.md) и
> [точный набор synthesis sources](development-cp67.md).
> CP67b имеет 1005 uwords, ROM decode и выключенный instruction prefetch.

Текущее дополнение CP36: [побитовый opcode index и aligned-word read
interface](area-decode.md). HC1200 включает `ALIGNED_WORD_READS=1`: opcode
ROM получает полное слово, operand byte mux находится перед engine.
Default=0 сохраняет прежний core interface; новых стадий и тактов нет.

> CP50, решение от 2026-09-11: default сборка снова явно MMU-less, VA=PA16.
> MMU-прототип сохранён под `UJ11_MMU` и отложен из-за ресурсов HC1200.
> [Профили сборки](build-profiles-cp50.md). FP11 удалён, FIS сохранён.


CP27 добавляет FADD/FSUB/FMUL/FDIV через T0…T7 и Q: 223 микрокоманды,
31 linking JUMP, всего **954×36 v12**. Decoder выделяет 32 FIS encodings
к entry 011. Pair5 расширен до D/Q для дешёвых Q+1/Q|1/Q+32;
старый Z/Q при D=0 эквивалентен прежнему datapath. Счётчик остаётся в RF,
нет новых FF/EBR и аппаратных arithmetic units. Все 700 старых words и
271 label сохранены, sequencer/PSW/memory RTL не меняются. [FIS](fis.md).

CP26 добавляет DIV: 58 слов, всего **700×36 v11**, без изменения datapath.
Общий MUL/DIV class и direct IR[9] dispatch к 072/076; S читается до
делимого, 16 итераций T4:Q restoring divide, signed correction и flags.
[Алгоритм, even/odd профиль и измерения](eis-div.md).


CP25 добавляет MUL dispatch 062/066 и 45 микрокоманд. T4:Q — signed product,
16 итераций RFQ_L/ADD/ADC, затем correction для отрицательного R и NZVC.
RF/ALU/Q/sequencer/PSW/FRAM RTL побайтно сохранены. Всего **642×36 v11**.
S читается до R; ошибки operand READ предшествуют flags/result commit.
[Обоснование по DEC/AM4, C correction и проверка](eis-mul.md).

CP24 добавляет XOR comparator и прямой destination mode dispatch к entry
02c/02e. Register path выполняет XOR RS/RD → RD с NZV за один execution
cycle. Memory path: CALL destination EA, READ, XOR RS/MDR → T2, WRITE,
прежний BIS_MEMORY_FLAGS. Поздний RS сохраняет J-11 alias ordering;
NZV меняются только после успешного WRITE. Пять новых микрокоманд, без новых
регистров, ALU mux, RF ports или полей слова; **597/1024×36 v11**.
[Семантика, тесты и реальные fit results](eis-xor.md).

CP23 меняет только combinational выбор следующего microaddress: общий
10-bit target, OR-dispatch в младших трёх bits и прежние overrides для
CALL/RETURN/WAIT/dispatch/faults. State-update block и весь остальной datapath
сохранены. Эквивалентность доказана; все cycle counts совпали с CP22.
[Подробности area gate](area-sequencer.md).

## Синхронный microstore без лишнего такта

`uj11_microseq` содержит uPC10, link10 и link_valid. На каждом разрешённом
фронте одновременно выполняются uPC←next_address и uword←ROM[next_address].
Поэтому uPC всегда соответствует текущему uword. В CP16 ROM/uPC используют `advance`, datapath — `step`. При обычном ходе
они совпадают; memory fault меняет uPC без failed-operation commit. Ожидание
ACK сохраняет uIR/uPC. `fault_inc` допускает одну проверенную ALU continuation.
Reset принудительно читает address 0 и запрещает datapath writes.

```
current uword ──► next-address logic ──► ROM address
      ▲                                 │
      └───────── next clock ─────────────┘
```

NEXT — 10-bit increment; PAGE — high2 от uPC плюс low8 из microinstruction;
JUMP/CALL — полный target. OR-dispatch использует IR bits непосредственно.
CALL имеет одну return slot; overflow/underflow ведут в diagnostic STOP.
CJUMP читает старые PSW/Q/condition inputs, возможна инверсия условия.
Отдельный hardware loop counter отсутствует, его predicate привязан к 0.
В CP21 ASH хранит count в T0 и использует ALU DEC/CJUMP; RF/Q/ALU/
sequencer не изменены. [Algorithm и измерения](eis-ash.md).

Microstore: четыре явных DP8KC 1024×9. INITVAL содержит пары 9-bit words
в 20-bit slots; bits18/19 каждого slot нулевые. ADA12..3=address9..0,
ADA2..0=001, REGMODE=NOREG. NOREG здесь не означает asynchronous ROM:
чтение происходит по фронту CLKA. Portable model и vendor model проверены.

## Один execution cycle

RF16×16: два asynchronous reads, synchronous write по адресу B.
В MAP/Synplify он реализован четырьмя DPR16X4C и четырьмя SPR16X4C,
без EBR и без 256 FF. Reset microcode очищает 16 слов через обычный write port.
Далее JUMP на FETCH: всего 17 initialization microclocks после снятия reset.
PC хранится только в R7; mirror и третий RF port не добавлены.

A/B fields имеют одинаковое расположение в ALU и control words. Literal
selectors 0..15 выбирают R0..R7/T0..T7; RS/RD и RS1/RD1 используют IR.
В v3 FETCH имеет command=2, совпадающий с ALU ADD, и кодирует AD/RF/TWO
в обычных datapath fields. Поэтому отдельные override mux для operation,
pair и D-input устранены. READ/WRITE и другие control words не разрешают
RF/Q/PSW writeback, несмотря на произвольные overlapping ALU fields.

Operand selection — параллельные masked buses. Один общий 17-bit carry
chain обслуживает ADD/ADC/SUB/SBC. Carry для SUB — borrow. Нет multiplier,
divider или barrel shifter. Q 16 поддерживает load и связанные RF/Q shifts.
PSW 16 обновляется KEEP/NZV/NZVC/LOAD; FLAGS берутся до RFQ writeback shift.

При FETCH completion IR и MDR получают входящий opcode, ALU записывает
R7+2. `dispatch_ir` в этот момент равен входящему memory word. Decoder
выбирает execution entry прямо на том же фронте ROM; decode bubble нет.
MOV/ADD/CMP R, R и BR заканчиваются одной ALU microinstruction с seq=FETCH.
Запись R7 этим же datapath перенаправляет следующий FETCH.

Измеренный цикл при zero-wait RAM:

```
FETCH: request/ack + R7+=2 + IR/MDR + ROM dispatch
EXEC:  dual RF read + ALU + RF/PSW commit + retire + ROM[FETCH]
```

Получены 2 clocks/instruction, включая BR и PC operands. Это не prefetch:
следующий memory request появляется в следующей FETCH-микрокоманде.

## Memory, fault entry и diagnostic stop

`uj11_mem` не содержит address registers или translation. Текущие uword/RF
удерживают address и data до ACK. Один request&&ack на фронте — один beat;
back-to-back beats допустимы. Byte transaction использует точный addr и
right-justified payload; в CP10 проверены также byte ISA и все addressing modes.

Word по нечётному адресу подавляет request; ACK+error запрещает
PC/IR/MDR/PSW/Q/RF side effects. В CP16 первая ошибка вызывает vector004,
а внутри construction frame фиксируется fault_code=1/2 и stopped.
До CP15 неподдержанный opcode выбирал STOP, code=3. Теперь decoder вызывает
vector010 либо 004 для invalid JMP; padding STOP и ошибки самого sequencer
остаются diagnostic. STOP не является PDP-11 HALT.
Reset очищает fault. `retire` — registered pulse после execution commit.

До CP14 IR16, MDR16, PSW16, Q16, sequencer21, fault2 и retire1 дают 88 state bits.
В историческом probe до CP14 дополнительно 18 input и 171 observation FF: всего 277 FF,
совпадающих с MAP. LUT count указан только для полного измеренного probe.
Выходы debug показывают RF write port и state; дополнительного RF read port нет.

## Что измерения говорят о конструкции

Формат/ROM занимают ровно четыре EBR. RF обеспечивает нужные два чтения и
одну запись за execution cycle. В M0 decoder не определял critical path; в CP9f FETCH path через
I/O mux и opcode dispatch стал худшим путём (см. ниже).
Control-field mux были дорогими: выравнивание selectors и ALU-поля FETCH
дали измеренное уменьшение LUT и рост Fmax. Оптимальность всего datapath
не доказана: сохраняется путь EBR → RF selector/read → ALU → RF/PSW.
Подробные timings, сохранённые неудачные варианты и следующий gate —
[synthesis.md](synthesis.md). Усложнений ради будущего MMU нет.

## CP6: FRAM stream

Core M0 не менялся; `uj11_fram_system` добавляет внешний memory unit.
MEMORY_MODE=0 — замороженный legacy `lsi11-fpga` контроллер, 1 — retained
sequential READ, 2 — тот же транспорт с однословным prefetch (по умолчанию).
FPGA адрес 16-bit; SPI header имеет high byte=0. I/O page выделяется до FRAM.

`uj11_fram_transport` получен из существующего небольшого byte FSM с тремя
дополнительными входами keep/resume/close. READ сохраняет CS после word,
resume начинает следующие два data bytes без повторных command/address.
Writes используют обычные WREN+WRITE и завершают CS. Закрытие обычного
pending read происходит только после полного слова; reset отдельно отменяет
текущую передачу. Внешняя FRAM не имеет ACK/CRC: отсутствие микросхемы этим
протоколом не диагностируется. Core bus_error остаётся отдельным контрактом.

`uj11_prefetch` хранит 15-bit aligned tag следующего слова, prediction-valid,
owner (idle/demand/speculative), PF_VALID и сохранённую ошибку. Слово PF_DATA
использует rdata 16 транспорта. Пока буфер полон, следующий SPI transfer
запрещён. Matching demand может получить ACK непосредственно на готовом
speculative response или из parked buffer. Нет сдвига IR/PC при speculative
чтении. READ stream qualifier позволяет брать extension в MDR.

При PC write совпадающий tag сохраняется, остальные predictions сбрасываются.
Сравнение ALU result с tag управляет только registered validity. На любом
PC-write edge новый prefetch не стартует; SPI close видит сброс validity
следующим тактом. Это убрало failed 35.176 ns / 25-level ALU→compare→CS path.
Data read/write и весь I/O page сбрасывают prediction, SMC не оставляет
устаревшее слово. Завершённое отменённое чтение не даёт архитектурного ACK.

Final CP6: 843 LUT4, 392 FF, 4 EBR; 19 stimulus + 176 observation FF входят
в probe. Отдельного bare-core+FRAM fitted LUT result нет. Периферия проверена
в testbench с настоящим legacy RTL, но не включена в этот synthesis top.


## CP7: EA без расширения datapath

Word MOV/ADD/CMP используют общий source/destination microcode через OR_MS/MD
и последовательные CALL/RETURN. Fast RR entries сохраняют один ALU execution
cycle. RF/Q/ALU/PSW/IR/MDR/microsequencer RTL не менялся. Изменён маленький
entry decoder и добавлены FRAM stream qualification/prefetch policy.

Всего 143 microcode words. T0 хранит source, T1 destination EA, T2 operand/result,
T3 pointer scratch либо старый destination для flags после WRITE ACK.
Control bit4 задаёт разрешение speculative launch, наследуемое через один FF
в `uj11_prefetch_control`. Это устранило лишние speculative word reads перед
operand transfers; retained READ и demand accesses продолжают работать.

Итог: core+probe 583 LUT4/277 FF/4 EBR, 40 MHz PASS. Core+FRAM+probe
861/393/4, 29.56 MHz PASS, Fmax 30.658 MHz. Scope не включает всю периферию.
Алгоритмы, J11 alias semantics и ограничения abort описаны в
[addressing-modes.md](addressing-modes.md).

## CP8: обратная пара operands

Pair7 изменена с DZ на BA, encoding v4. PASSA/DA выполняет прежние D loads;
BA читает B на левый вход ALU и A на правый, сохраняя запись по B. Это
позволяет SUB/BIC R, R за один execution cycle, без изменения ALU/flags/RF.
Decoder принимает семь word double-operand classes, microcode — 214 words.
FRAM transport и pause policy не менялись. Core + probe стоит 600/277/4,
core + FRAM + probe — 878/393/4. Подробности и timing limits:
[word-double-operand.md](word-double-operand.md), [synthesis.md](synthesis.md).

## CP9: unary и branch microcode

Добавлены 86 unary и 42 branch words, всего 342. Unary mode0 entry выполняет
одну ALU microinstruction с dynamic RD; остальные modes вызывают общий
DESTINATION_EA и отдельный READ/WRITE tail. Старый operand хранится в MDR/T0
до WRITE ACK, затем ALU повторяет computation для фиксации flags. CLR не
читает конечный destination, TST не пишет. Дополнительных state bits нет.

Branch dispatch использует {IR[15], IR[10:8]} напрямую. N/Z/V/C compounds
выражены существующими CJUMP; идеальная задержка branch 3..5 clocks,
BR — 2. CJUMP ставят prefetch=0 до redirect decision, предотвращая лишний
FRAM READ на taken branch. Измеренный tradeoff и algorithm — в
[single-operand-branches.md](single-operand-branches.md).

Final core+probe 615/277/4, 40 MHz PASS; FRAM+probe 910/393/4,
29.56 MHz PASS. В FRAM worst path теперь проходит от microstore через
RF address/I/O-page decode, rdata mux, opcode dispatch и next-address назад
к microstore: 32.414 ns, 15 levels. Поэтому isolated decoder Fmax M0
не доказывает запас полного FETCH path при расширенной ISA.


## CP10: byte datapath и prefetch timing

RF не расширен: один full-word writeback либо сохраняет high byte (OPERAND),
либо знаково расширяет MOVB. Byte carry вычисляется из bit8 единственного
adder; flags выбирают bit7/15, right shifts вводят sign/carry в bit7.
EA transfers остаются word; только direct autoincrement/decrement R0–R5
меняют шаг на 1. 342 общие microinstructions покрывают word и byte ISA.

После byte merge путь EBR→RF→ALU→PC compare→valid CE не прошёл 29.56 MHz.
Четыре группы compare тоже не помогли. Один FF `redirected` принимает
PC mismatch и непосредственно маскирует prediction/buffer valid; хранимые
valid states очищаются следующим edge. FETCH increment исключён через
consumed, matching PC write сохраняет buffer, speculation на PC-write edge
по-прежнему запрещена. Все 430 прежних benchmarks сохраняют cycle counts.

Final CP10j core+probe: 659/277/4, 35 MHz PASS, TRACE 36.496.
CP10k FRAM+probe: 905/394/4, 29.56 MHz PASS, TRACE 32.047.
Это +1 FF относительно CP9 FRAM и −5 LUT, без дополнительного RF port,
ALU pipeline stage, PC mirror или MMU. См. [byte-instructions.md](byte-instructions.md).


## CP11: subroutine control и раннее завершение SOB

JMP/JSR вызывают существующий destination EA; отдельного EA engine нет.
JSR push/link/target и RTS target/pop выполняются обычными RF/READ/WRITE
микрокомандами. R6/R7 aliases и изменение SP проверены против DCJ11.

ALU seq3 `FETCH_A1` завершает instruction при исходном RF[A]==1, иначе NEXT.
SOB пишет Rs−1 с flags=KEEP и затем, если нужно, PC−unsigned offset×2.
Это 2/3 CPI с ideal RAM против 8/9 у PSW-save baseline. Проверка результата
ALU Z давала тот же CPI, но не прошла core 35 MHz; ранний predicate проходит.
D=DISP маскирует два high offset bits и sign по IR[14]; branch остаётся signed.
Prefetch приостанавливается на conditional-retire ALU word, FF не добавлены.

355 words, encoding v7. Core+probe 694/277/4, 35 MHz PASS, TRACE 36.358;
FRAM+probe 970/394/4, 29.56 MHz PASS, TRACE 31.672. Все 606 CP10 benchmarks
сохраняют counts. Подробности aliases, exclusions и rejected v6 —
[control-flow.md](control-flow.md).


## CP12: byte swap без нового datapath

SWAB использует восемь имеющихся RF/Q shifts; SXT — CJUMP N и MOV tail,
MARK — DISP/ADD и обычный stack READ. Это 38 новых microinstructions при
неизменных RF/Q/ALU/PSW/sequencer/memory RTL. Все 355 CP11 words сохранены.

Изменился только opcode decoder: parallel masks вместо priority chain.
При расширении ISA priority variant дал FRAM 1081 LUT; parallel masks —
984 LUT. Core имеет 708 LUT в обоих случаях. Это измеренный результат
оптимизации полного scope; LUT price не выводится из одного isolated decoder.
Final core 708/277/4, 35 MHz PASS/35.723; FRAM 984/394/4,
29.56 MHz PASS/31.287. [Рутины и проверки](extra-instructions.md).


## CP13: vector entry на прежнем datapath

BPT/IOT/EMT/TRAP выбирают vector через IMM→T4; общая routine читает
vector+2/vector, сохраняет old PSW/PC на стеке и загружает новый PC.
RTI снимает PC/PSW через два READ ACK. Добавлены 29 words, всего 422;
RF/Q/ALU/PSW/sequencer/memory RTL функционально не изменены.

Core 735/277/4, 35 MHz PASS/36.340; FRAM 999/394/4,
29.56 MHz PASS/31.245. 17 CPI ideal RAM для software trap, 8 для RTI.
Ошибки frame/return терминальны; IRQ, trace и mode/bank exchange ещё отсутствуют.
Профиль и exact bus verification: [software-traps.md](software-traps.md).


## CP14: IRQ/WAIT без дополнительного PC или vector register

Sequencer подставляет IRQ entry013 вместо ALU FETCH020 при допустимом
priority. Для FETCH_A1 этот выбор находится перед ранним A==1 predicate;
поздний результат ALU не включён в IRQ dispatch. MDR захватывает vector на
принимающем фронте, после обычного commit предшествующей инструкции.
`irq_active` исключает retire IRQ frame; `wait_seen` исключает повторный
retire при WAIT. Итого 90 core state FF, RF по-прежнему distributed RAM.

Core probe: 30 input + 173 observation + 90 state = 293 FF.
Generic FRAM probe: 31 input + 178 observation; всего 410 FF.
KW11/KL11 resolver probe: 29 input + 179 observation; timer pending добавляет
один рабочий FF, итог тоже 410. Разница probes существенна для сравнения LUT.

24 новых words, 446 v8. SPL использует direct IR[2:0] dispatch и прежний
ALU mask/OR; reset PSW теперь 0340. Core 764 LUT/35 PASS/36.720 MHz;
FRAM+adapter 1016 LUT/29.56 PASS/31.221 MHz. [IRQ details](interrupts.md).


## CP15: compact fallback dispatch

Нативные classes decoder сохранены. Для fallback уже вычисленный признак
JMP opcode выбирает bit1 у адресов 040/042. Это устраняет добавление отдельного
invalid-mode class в общий valid mask; RTL miter проверяет все 65536 encodings.
Оба двухсловных entries загружают vector в T4 и используют старый TRAP_ENTRY.
Новые state bits отсутствуют; microstore 450 words v8, все прежние 446 сохранены.

Core778/293/4, 35 PASS/36.236 MHz; FRAM+resolver 1036/410/4,
29.56 PASS/31.697 MHz. [Варианты и область совместимости](reserved-traps.md).

## CP16: fault redirect и одна repair microinstruction

`frame_active` ставится control15=TRAP и очищается terminal ALU boundary.
Первый bus_fault перенаправляет sequencer, очищает CALL link и использует
двухбитный `irq_active` для подавления retire и раннего IRQ; fault внутри frame фиксирует stopped.
READ bit2=fault_inc позволяет исполнить target ADD STEP/TWO перед 015.
Флаг fault_repair очищается после этой ALU word. В успешном ходе никаких
дополнительных clocks, mux для EA или дублирования PC не появилось.

Core: 30 stimulus + 173 observation + 93 state = 296 FF. Три новых state FF:
frame_active, fault_repair и старший bit irq_active. IRQ ожидает завершения
первой инструкции fault handler. Final core 809 LUT, FRAM+resolver 1042 LUT/413 FF;
оба 4 EBR. 452 words v9. [Semantics/verification](memory-faults.md).

## CP17: trace snapshot и общий возврат RTI/RTT

`trace_latched` сохраняет PSW.T при успешном FETCH. На boundary обычной
инструкции используется этот snapshot; terminal 037 с bit0=1 выбирает
восстановленный PSW.T для RTI или suppress для RTT через IR[2]. D=IMM
сохраняет прежнюю интерпретацию младшего байта и не включает return policy.
WAIT рассматривает T после первого retire, используя прежний `wait_seen`.

Trace имеет приоритет перед IRQ и выбирает прежний entry 024. Значение
`irq_active=3` запрещает дополнительный retire/trace/IRQ самого trace frame;
handler FETCH снова фиксирует T. `frame_active` защищает frame от рекурсии
при второй memory error. Профиль — один kernel set, CM=PM=RS=0, NZVC/IPL/T.

Core: 30 stimulus + 173 observation + 94 state = 297 FF, 826 LUT4/4 EBR.
FRAM/resolver probe: 1046 LUT4/414 FF/4 EBR. Изменён только один word bit,
452 слова сохранены. [Подробная semantics и проверка](trace-rtt.md).

## CP18: CC mask без нового D mux

IR[3:0] напрямую образуют uaddress[6:3] при base104. Mask приходит из
36-bit ROM в T4, затем OR_MD выбирает PSW BIC/OR. Datapath и sequencer
сохранены побитно. MFPT — одна обычная ALU microinstruction с IMM5.
[Размещение, стоимость и ограничения](system-flags.md).

## CP19: PSW transfer через существующий ALU

MFPS register path — PASSA/DA/D=PSW/B=RD/dst=MOV/flags=NZV, один execution
word. Memory path вызывает DESTINATION_EA, сохраняет PSW в T0 и использует
общий MOV_MEMORY; flags обновляются только после успешного byte WRITE.
MTPS получает operand в T2, создаёт mask00ef в T4, маскирует operand,
инвертирует mask, маскирует старый PSW и OR/LOAD объединяет части. Отдельный
terminal ALU word нужен для IRQ по новому IPL. T сохранён, trace snapshot
не меняется.14 новых words,507 всего; hardware datapath/state прежние.
[MFPS/MTPS](psw-transfer.md).

## CP20: HALT restart / peripheral RESET

521 words v11, RF/Q/ALU сохранены; добавлен один FF выхода RESET.
Control JUMP bit0=init фиксирует peripheral_reset на следующий clock
(settling ALU); terminal ALU проверяет IRQ после снятия импульса.
Сигнал идёт в reset периферии и IRQ pending latch, но не в reset CPU/FRAM.
HALT использует TRAP frame guard, сохраняет PSW/PC, читает004, очищает PC bit0,
загружает PSW0340. Это профиль нашего DCJ11 emulator, не полный native ODT.
Старые507 microinstructions и весь datapath/sequencer сохранены.
[Контракт, проверка actual peripherals и ограничения](system-control.md).

## CP22: ASHC

ASH/ASHC разделяют один 6-bit opcode comparator и прямой IR9 dispatch.
Обе инструкции читают целевой регистр/пару после вычисления EA счётчика;
все autoincrement/autodecrement side effects уже учтены. ASHC формирует
N/Z по полному 32-bit результату, независимо от aliased stores при odd Rs.
T4:Q сдвигается прежними RFQ_L/R; count хранится в RF. На один bit — 5 cycles.
Новых полей и состояния нет; microstore CP22 — 592×36.
[Алгоритм, исправление C oracle и проверки](eis-ashc.md).


## CP28 board specialization

Default `ROM_DECODE=0` сохраняет прежний combinational dispatch и latency.
Physical HC1200 top выбирает `ROM_DECODE=1`: successful FETCH ACK захватывает
IR/MDR и читает 1024×9 dispatch EBR; один внутренний clock позднее исполняется
PC+2/dispatch, без второго memory request. Failed FETCH идёт прежним fault path.
Второй opcode register не добавлен. Decoder address compression проверяется
на всех 65536 encodings; microstore по-прежнему 1024×36 с 954 занятыми words.

ALU Boolean/arithmetic/left/right paths формально эквивалентны CP27. RF/Q и
microsequencer не расширены. Параметры `IRQ_VECTOR_BITS=15` и
`UNMASKED_VECTOR=160000` используются только платой для private RK assist;
default external IRQ profile остаётся 8-bit/обычный IPL. Transport, firmware
mapping и ограничения описаны в [CP28 integration](hc1200-integration.md).
