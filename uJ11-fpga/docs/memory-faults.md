# CP16: ошибки памяти и vector 004

CP16 переводит odd-word и `ack && error` в общий микрокодный trap по vector
004. Адрес остаётся 16-bit, logical == physical; MMU отсутствует.
Профиль тот же: один kernel register set, CM=PM=RS=T=0, NZVC/IPL.

## Контракт и состояние при ошибке

Неудачная memory microinstruction не записывает RF, Q, PSW, IR или MDR.
ROM/uPC при этом могут перейти на fault entry: `advance` отделён от `step`,
который разрешает architectural commit. `uj11_mem` выдаёт fault только для
нечётного word или завершённого ACK с error; в обоих случаях `complete=1`.
Нечётный word вообще не выходит на внешний bus, byte по нечётному адресу допустим.

Сохраняется текущий архитектурный R7: неуспешный FETCH не увеличивает PC;
operand fault сохраняет уже выполненные изменения PC/EA. Дополнительной
копии instruction PC, третьего RF port или аппаратного EA engine нет.
Занятый CALL return slot очищается при redirect, поэтому обработчик может
вызывать те же addressing routines. Ошибка имеет приоритет над pending IRQ:
неудачная инструкция и её fault frame не создают `retire` или `irq_ack`.
Даже если vector PSW загружает IPL0, IRQ ждёт завершения первой handler
instruction; при IPL7 он остаётся замаскированным до соответствующего изменения
PSW. Обработчик и RTI исполняются обычным микрокодом.

Общая последовательность: READ vector+2, READ vector, LOAD нового PSW,
SP-=2/WRITE старого PSW, SP-=2/WRITE старого PC, PC←новый vector PC.
`BUS_FAULT_ENTRY` занимает 015/016: IMM4→T4 и `TRAP TRAP_ENTRY`.
После неуспешного memory edge до handler FETCH — 16 microclocks при
zero-wait vector/stack memory, плюс один при восстановлении autoincrement.
Время failed transfer и предшествующего вычисления операнда сюда не входит.

## Autoincrement без штрафа успешному prefetch

DCJ11 `decode_data` увеличивает регистр mode2/mode3 до operand/pointer READ.
У uJ11 успешный source mode2/3 и destination mode3 читают до увеличения,
чтобы immediate/absolute могли использовать уже prefetched слово по старому PC.
Первый вариант CP16a/b поэтому неверно сохранял регистр при ошибке этих READ.
Контрольный запуск архивного CP16a с новым oracle воспроизводит
`R0=2000`, когда reference требует `2002`; он сохранён как **отрицательный** тест.

В принятом варианте READ bit2=`fault_inc=1` разрешает после failed edge
исполнить ровно одну continuation microinstruction, затем перейти на 015.
Это прежний `ADD STEP/TWO` к выбранному регистру. `fault_repair` — один FF;
неуспешные данные не попадают в MDR. Ассемблер разрешает только ADD в тот же
регистр, pair=AD, dst=RF, flags=KEEP, seq=NEXT, d=STEP/TWO и отвергает
конфликтующие continuations. Flag установлен только в READ по 224/230/2a8.
Успешные instructions сохраняют прежние ROM addresses, microclocks и SPI counts.

## Защита построения frame

Control15=`TRAP` имеет тот же target, что JUMP, и устанавливает `frame_active`.
Им заменены семь переходов в общий frame: четыре software traps, IRQ,
reserved и invalid JMP. Флаг сбрасывается на завершающей ALU boundary.
Вторая memory error внутри vector fetch/stack push остаётся terminal
`stopped`: code1 odd word, code2 ACK error. Padding/ошибка sequencer — code3.
Это явно заданный v1 double-fault policy. DCJ11 red-stack emergency frame,
yellow-stack limits, mode/bank exchange здесь не реализованы.

`irq_active` теперь двухбитный: 0=обычная инструкция, 1=IRQ frame,
2=memory-fault frame. Состояние 2 блокирует IRQ на завершающей boundary frame;
handler FETCH не является точкой приёма IRQ. Отдельного 16-bit vector/PC register
нет. Всего +3 state FF к CP15: frame_active, fault_repair и старший bit irq_active.
RF, Q, ALU, decoder, PSW и FRAM transport сохранены.

CP16c/d с одним общим признаком async frame принимали unmasked IRQ слишком
рано. Отрицательный тест архивного CP16c воспроизводит это. Отдельный
[`fault_irq_order.c`](../tb/fault_irq_order.c) проверяет 8 сценариев исходного
DCJ11 core с IPL0/IPL7: первая handler instruction предшествует IRQ.
Принятый CP16e/f соответствует этому порядку, сохраняя immediate nested IRQ
после IRQ-frame, ранее проверенный в CP14.

## Проверка с настоящим emulator

Первичный источник — существующий [`core/core.c`](../../core/core.c):
`bus_error_trap`, `decode_data`, `core_take_vector`, FETCH, `pullw`.
Исходный emulator не изменён. [`fault_oracle.py`](../tools/fault_oracle.py)
создаёт build copy с read-only hooks: четыре memory entry, vector entry и
конец успешного vector frame. Обратное удаление hooks проверяется.
ACK ошибки вводятся через public bus callbacks и `core_bus_error_trap`;
odd-word использует собственную проверку DCJ11.

**32780 завершённых vector-boundary cases**: 28256 ACK errors и 4524 odd-word;
3536 cases требуют autoincrement repair. Проверены registers/PSW/PC и 213080
точных bus beats, включая порядок frame и failed write data. Восемь кандидатов
`SUB @-(PC), R1` / `SUB R0,@-(PC)` затрагивают internal DCJ11 I/O и исключены;
их opcodes/PSW/reasons сохранены в CSV. Это не passing cases.

Сравнение заканчивается на успешном frame, перед первой handler instruction.
В 296 случаях исходный C emulator после этого продолжает изменять registers/PSW;
четыре RTI first-pop faults также дают дополнительное чтение. `pullw` и RTI
продолжаются после callback abort. Эти наблюдения сохранены отдельным CSV;
они не подменены ожидаемым uJ11 результатом и не названы post-instruction
совместимостью. FETCH, напротив, очищает transient `fAbort` перед return;
поэтому завершение определяется по vector hooks, а не этому флагу.

Все 32780 cases прогнаны целиком на RAM/FRAM × portable/vendor DP8KC.
Последовательность case IDs и все per-case CSV совпали.
`make verify-cp16` воспроизводит оба ROM-варианта и сохраняет evidence.

Дополнительно:

* 96 directed double-fault cases: четыре позиции ошибки frame, odd SP,
  failed FETCH/operand, odd PC, 0..3 waits; отсутствие recursive frame/retire.
* 32 теста реального `uj11_fram_system`: I/O READ/WRITE error и odd guard,
  полный frame, EA CALL в handler, RTI, held IRQ при vector IPL0/IPL7, последующий quiescent WAIT.
* Старые CSR fault tests проверяют состояние у BUS_FAULT_ENTRY; их сокращённые
  memory maps не изображают trap memory. Полный frame проверяется новым suite.
* Все 124969 прежних completed-instruction cases и 726 benchmarks на ROM
  сохранены; все десять per-case cycle traces идентичны CP15.

Fault injection перед RAM/FRAM моделирует ошибку логического slave: failed
WRITE не передаётся transport. MR45V100A не имеет ACK/CRC для обнаружения
отсутствующего/неисправного чипа. Реальный I/O `io_error` проверен отдельно.
Timeout для I/O, который совсем не отвечает, ещё не добавлен.

## Измерения HC1200

| Вариант | LUT4 | FF | EBR | Constraint / результат | TRACE Fmax |
|---|---:|---:|---:|---|---:|
| CP16a core, без repair — отклонён по semantics | 791 | 294 | 4 | 35 MHz PASS | 36.831 MHz |
| CP16b FRAM/resolver, без repair — отклонён | 1062 | 413 | 4 | 29.56 MHz PASS | 31.083 MHz |
| CP16c core, repair, но ранний IRQ — отклонён | 792 | 295 | 4 | 35 MHz PASS | 36.988 MHz |
| CP16d FRAM/resolver, ранний IRQ — отклонён | 1030 | 412 | 4 | 29.56 MHz PASS | 31.079 MHz |
| CP16e core, final | 809 | 296 | 4 | 35 MHz PASS | 36.552 MHz |
| CP16f FRAM/prefetch/resolver, final | 1042 | 413 | 4 | 29.56 MHz PASS | 31.117 MHz |

452 words/1024×36, encoding v9. Из 450 CP15 words изменены только семь TRAP
commands и три fault_inc bits; все labels сохранены, добавлены 015/016.
Снижение LUT FRAM относительно более простого CP16b — результат полного
mapping/packing, а не отрицательная логическая стоимость repair FF.
Относительно CP15e/f: core+31 LUT/+3 FF, FRAM+6 LUT/+3 FF, EBR без изменения.

Это прежние resource probes, с observation/stimulus FF и IRQ resolver.
Полные UART/timer/panel/SD/RK и external pin timing не входят в scope.
238 свободных LUT и 3 EBR в CP16f не являются гарантией полного board fit.
50 MHz остаётся незакрытой целью; плата не программировалась.
[Отчёты](synthesis.md), [manifest](verification-cp16.json), [cycles](benchmarks-cp16.json).
