# CP15: reserved и invalid-mode traps

К CP14 добавлены четыре microinstructions и изменён только opcode decoder.
Новый hardware trap FSM или state register не нужен. Профиль остаётся
kernel bank0, CM=PM=RS=T=0, адрес 16 бит, без MMU.

* `JMP Rn` с mode0: vector **004**, entry **040**.
* Native reserved instructions и `JSR` с destination mode0: vector **010**,
  entry **042**.
* Все прочие ещё не реализованные opcode classes также направляются на010.
  Это политика неполного ISA; она не означает совместимость с DCJ11 для
  этих инструкций. В частности, HALT, RTT и EIS пока не реализованы.
* Odd memory access и ACK+error сохраняют **diagnostic STOP**. Наличие
  vector004 для invalid JMP не означает, что architectural bus faults уже работают.

Правила различия JMP/JSR проверены в первичном `core/core.c`, в обработчиках
`JMP`, `JSR`, `illegal_trap` и `trap_vector4`. Sibling emulator не менялся.

## Последовательность

Каждый entry делает IMM→T4 (4 либо 8), затем JUMP на прежний TRAP_ENTRY.
Далее CP13 microcode читает vector+2/vector, сохраняет old PSW и PC на стеке,
загружает target PC. Сохраняется PC за ошибочным opcode. Успешный frame
даёт один retire, RTI возвращает прежние PSW/SP/PC. ACK errors внутри frame
терминальны, как в CP13/14; повторный frame по ошибке не начинается.

Микрокод: **450/1024 × 36 bits**, 43.9453125%, encoding v8, четыре EBR.
Все 446 CP14 words и label addresses сохранены. Invalid mode не вызывает
EA routine, не читает operand и не меняет link register JSR.

## Сравнение вариантов HC1200

| Gate | Изменение | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---|---:|---:|---:|---|---:|
| cp15a | Отдельный invalid-JMP mask, core | 772 | 293 | 4 | 35 MHz PASS | 36.476 MHz |
| cp15b | То же, FRAM + IRQ resolver | 1049 | 410 | 4 | 29.56 MHz PASS | 31.944 MHz |
| cp15c | Entries 3fa/3fc, core | 779 | 293 | 4 | 35 MHz PASS | 35.828 MHz |
| cp15d | Entries 3fa/3fc, FRAM + resolver | 1043 | 410 | 4 | 29.56 MHz PASS | 30.929 MHz |
| **cp15e** | Shared JMP predicate, core | **778** | **293** | **4** | **35 MHz PASS** | **36.236 MHz** |
| **cp15f** | Shared JMP predicate, FRAM + resolver | **1036** | **410** | **4** | **29.56 MHz PASS** | **31.697 MHz** |

В базовом FRAM variant Synplify ORCALUT4 вырос с 899 у CP14c до 933:
прирост появился до routing. Перенос entry в верхнюю часть ROM дал только
−6 LUT в FRAM scope, увеличил core и уменьшил timing margin.

Принят e/f с прежними entries040/042. Уже имеющийся `jump_opcode` выбирает
один bit fallback address, а новый invalid-mode class не добавляется к
общему valid mask. Это даёт −13 LUT у FRAM против b ценой +6 LUT у core;
оба проходят прежние clocks. Относительно CP14: +14 core / +20 FRAM LUT,
FF/EBR без изменения. Полная таблица decoder e/f совпадает с a/b для всех
65536 opcodes; отдельный RTL miter сравнивает их с архивным baseline.

Worst CP15f path: EBR→RF/address selection→I/O data mux→opcode decoder/sequencer→EBR, 31.575 ns, 16 logic
levels. Core CP15e: EBR→ALU→PSW.Z, 27.258 ns, 17 levels.
Это probes с независимыми входами и наблюдениями, как CP14. Полные UART,
timer/panel/SD/RK и внешние pin delays не включены. Осталось 244 LUT/3 EBR
в измеренном FRAM scope; 50 MHz по-прежнему не достигнуты.

## Что именно сравнивается с DCJ11

Exhaustive decoder даёт 56268 ordinary ISA encodings и 9268 trap routes.
Для последних oracle пробует по два PSW состояния (000000 и 000357), всего
**18536 кандидатов**. В compatibility fixtures попадают только успешные
ожидаемые vector004/010 с корректным bus trace в kernel/T=0 профиле.

**4160 завершённых cases, 2080 opcode values:** 16 cases JMP mode0/vector004
и 4144 vector010. **14376 кандидатов исключены**: у reference это другая
инструкция или другой исход, а не соответствующий ожидаемый trap. Причины
могут пересекаться: 14376 different/no-vector, 394 abort, 550 I/O, 0 odd-word.
Они не называются passing/compatible cases. Наличие у uJ11 fallback на010
не превращает отсутствующие EIS/system/FPU instructions в реализованную ISA.

4160 cases проверяют registers/PSW/PC, memory changes и **20800 exact beats**.
RAM с 0..3 waits: **101920 microclocks**; SPI FRAM: **2533440**.
96 directed frame cases отдельно проверяют vector reads, stack writes,
odd SP и отсутствие failed-store commit. Ранее существовавшие 16 mode0
JMP/JSR directed cases обновлены с ожидания STOP на проверку frame/order;
остальные 42 control cases сохранены, всего по-прежнему 58.

Полная проверка: **124969 completed DCJ11 cases** на каждую комбинацию
RAM/FRAM × portable/vendor ROM, **726 benchmark runs** на ROM-модель.
Все 714 CP14 counts, девять fixtures и per-case cycles сохранены.
Новые loops reserved/RTI/BR, invalid-JMP/RTI/BR, invalid-JSR/RTI/BR: 9 CPI
ideal RAM, 346.333333 CPI с prefetch FRAM, 3 memory beats/instruction.
Это измерение полного loop; один такой trap занимает 17 clocks ideal RAM.

`make verify-cp15` воспроизводит tests, benchmark snapshots, decoder miter
и recorder. [Manifest](verification-cp15.json), [benchmarks](benchmarks-cp15.json).
Все шесть synthesis variants и полный проверенный source archive сохранены.
FPGA не программировалась. Следующий hardware gate — memory bus/address
vector004 с commit suppression, очисткой EA CALL link и frame-fault guard.
