# CP13: первый software-trap/RTI gate

Реализованы BPT, IOT, EMT, TRAP и RTI в ограниченном профиле: один kernel
register set, CM=PM=RS=T=0. Используются NZVC и IPL PSW[7:5]; текущие
IRQ inputs отсутствуют. Mode exchange, trace, RTT, HALT/WAIT, PSW instructions,
architectural bus/address/illegal traps ещё не реализованы. PSW за пределами
этого профиля не проверен и не объявляется совместимым с J-11.
MMU отсутствует; все memory/vector/stack addresses — 16-bit physical.

## Microcode и порядок frame

Первичный oracle — model DCJ11 из [`core/core.c`](../../core/core.c),
`ENABLE_MMU=0`. В profile без mode exchange его `core_take_vector` делает
READ vector+2 (PSW), READ vector (PC), PSW load, push old PSW, push old PC,
затем PC=new PC. Именно такой порядок проверяется по каждому bus beat.
Векторы octal: BPT 014, IOT 020, EMT 030, TRAP 034. Low eight bits EMT/TRAP
не меняют vector. PC в frame указывает за opcode.

Entries BPT/IOT/EMT/TRAP: 024/026/028/02a. Каждый загружает vector в T4
и переходит на общую routine 3d6. Она сохраняет old PSW/PC в T0/T1, принимает
new PSW/PC в T2/T3, пишет frame через SP и завершает instruction. T4 служит
адресом vector. Конечный frame: [SP]=old PC, [SP+2]=old PSW. Никакого
отдельного hardware trap FSM или дополнительного RF не добавлено.

RTI занимает 031..037: READ PC по SP, SP+=2, READ PSW, SP+=2, PSW load,
PC restore. Адрес 030 остаётся прежним CAPTURE_REGISTER_SOURCE. Попытку
занять его новым RTI assembler остановил как overlap до synthesis.
Существующие routines и их label addresses сохранены.

Все transfers word. EMT/TRAP имеют high opcode bit, но используют только
flags=KEEP/LOAD и dst=RF; byte ALU qualifier для них не включается. Все поля
36-bit v7 сохранены. Добавлены 29 words, всего 422/1024 (41.2109375%).
Для ideal RAM BPT/IOT/EMT/TRAP — 17 clocks, RTI — 8, включая FETCH.

## Ошибки и граница совместимости

Bus/address errors пока дают diagnostic STOP, включая ошибку внутри vector
fetch/frame/RTI. Они **не** перенаправляются автоматически на vector 004.
Неподдержанные opcodes также ещё не вызывают vector 010. Поэтому этот gate
не завершает Stage 2 и не является полным J-11 trap implementation.

Vector READ error сохраняет прежние SP/PSW/PC; неуспешная frame WRITE
происходит после PSW load и уменьшения SP. Уже подтверждённая предыдущая
WRITE сохраняется; неуспешная не меняет memory. RTI READ error сохраняет
old PC/PSW; SP увеличен только за успешно завершённое первое чтение.
Полная идентичность partial-abort state, CPUERR, red/yellow stack traps и
restart semantics DCJ11 не заявляется. Odd final PC диагностируется при
следующем FETCH, после успешного завершения самой trap/RTI instruction.

## Реальный synthesis checkpoint

| Scope | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---:|---:|---:|---|---:|
| CP13a core + probe | 735 | 277 | 4 | 35 MHz PASS | 36.340 MHz |
| CP13b core + FRAM/prefetch + probe | 999 | 394 | 4 | 29.56 MHz PASS | 31.245 MHz |

Diamond MAP/PAR/TRACE, HC1200. Относительно CP12: +27 LUT core, +15 LUT
FRAM, без новых FF/EBR. Из функционального RTL изменился только decoder;
комментарий D=DISP уточнён для уже существующего MARK. IRQ/trap metadata
registers и memory translation не добавлены. 281 LUT/3 EBR остаются у probe;
полная board периферия и внешние pin delays в эти цифры не входят.
50 MHz остаются целью. [Исходные reports](../synth/reports/cp13b/result.json).

## Verification

4640 завершённых oracle cases, ноль exclusions: 3584 software traps и 1056
RTI. Проверены все 256 code fields EMT и TRAP, все 128 допустимых NZVC/IPL
значений PSW в выбранном профиле, все old/new IPL pairs у RTI, odd/even PC,
stack/data overlap с opcode и граница FRAM/I/O. Это наборы покрытия, а не
полный Cartesian product всех machine states.

Oracle получает прежние четыре address hooks и vector-entry hook в build
copy; исходный emulator не меняется. Новый `ORACLE_TRAP_PROFILE` требует
ровно один ожидаемый vector entry для software trap и ноль для RTI.
У обычных instruction suites прежняя политика исключения traps сохранена.
RAM с 0..3 waits: 101008 clocks; SPI FRAM: 2523744 clocks; в обоих случаях
21088 exact bus beats, registers/PSW/PC сравниваются после completion.

112 directed cases покрывают успешный frame/RTI, ошибку каждого vector
READ, каждого stack WRITE, каждого RTI READ и odd SP, с 0..3 waits.
Проверяются порядок bus operations, стабильность request до ACK, запрет
byte/odd-word transfers, R0..R5, PSW, SP и отсутствие записи при failed ACK.

При RTI со SP на opcode обнаружено расхождение initializer: oracle записывал
opcode после patches, а fixture выдавал его перед patches. Теперь при
перекрытии bytes opcode выдаётся последним; все семь прежних CP12 fixtures
сохранены побайтно. Это исправление test data serialization, не ISA emulator.
[Первоначальный диагностический log](../tb/reports/cp13-fixture-order-initial.log).

Шесть benchmark workloads × четыре memory modes: четыре trap/RTI/BR loops,
вложенный BPT→IOT→RTI→RTI→BR и MOV #SP/RTI loop. После целых periods
проверяются PC/SP/PSW и R0..R5. Warmup исключён из counts.
[Таблицы](benchmarks.md), [JSON](benchmarks-cp13.json),
[verification manifest](verification-cp13.json).

`make verify-cp13` выполняет portable/vendor regression, сохраняет CSV/JSON
до vendor runs и вызывает recorder только после успешных команд. Для быстрых
отдельных проверок: `make test-trap test-trap-faults benchmark-trap` и соответствующие
vendor targets. Следующий hardware gate — IRQ boundary/acknowledge и HALT/WAIT,
с проверкой pending timer pulse во время длинной FRAM транзакции.
