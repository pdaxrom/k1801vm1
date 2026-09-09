# CP8: word double-operand ISA

Исторический checkpoint; текущие byte forms и итоговые цифры — в [CP10](byte-instructions.md).

Реализованы MOV/CMP/BIT/BIC/BIS/ADD/SUB со всеми source/destination modes.
BR сохранён. Общий алгоритм EA, DCJ11 register alias semantics и FRAM policy
из [CP7](addressing-modes.md) используются без аппаратного EA engine.
Микрокод растёт с 143 до **214 words**, RF остаётся 16×16.

## Одна execution microinstruction для всех R,R

RF записывает результат по адресу B. ALU SUB вычисляет A−B, BIC — A&~B;
PDP-11 SUB/BIC требуют обратного порядка. В encoding v4 pair7 **BA** подаёт
read_b на левый вход ALU и read_a на правый. Selectors остаются A=RS, B=RD,
поэтому запись по B сохраняет правильный Rd. ALU, carry chain, flags и
ширина microinstruction не менялись. Старую пару DZ удалили; D loads
используют PASSA/DA. Ассемблер отвергает старый DZ вместо тихого переименования.

| PDP-11 | ALU | Pair | Writeback | Flags | RR / EA entry, hex | Destination table, hex |
|---|---|---|---|---|---|---|
| MOV | PASSA | AB | Rd | NZV, C preserved | 110 / 118 | 300 |
| CMP | SUB | AB | none | NZVC, src−dst | 120 / 128 | 320 |
| BIT | AND | AB | none | NZV, C preserved | 130 / 138 | 360 |
| BIC | BIC | BA | Rd | NZV, C preserved | 140 / 148 | 380 |
| BIS | OR | AB | Rd | NZV, C preserved | 150 / 158 | 3a0 |
| ADD | ADD | AB | Rd | NZVC | 160 / 168 | 340 |
| SUB | SUB | BA | Rd | NZVC, dst−src | 1e0 / 1e8 | 3c0 |

Decoder принимает word classes 1..6 и E, использует те же direct opcode bits
в entry `{01,opcode,!rr,000}`. Ни opcode-specific ALU override, ни отдельный
hardwired execution path не добавлены. Высокий bit у SUB означает word SUB,
а не byte operation. Принимаются 7×4096 + 256 BR = **28928 encodings**.

## Memory operands, flags и I/O

Общие SOURCE_EA / DESTINATION_EA / CAPTURE_REGISTER_SOURCE и OR_MD
сохраняются. T0=source, T1=destination EA, T2=destination/result.
MOV выполняет только write конечного destination; CMP/BIT — только read;
BIC/BIS/ADD/SUB — один read и один write. Pointer/extension reads относятся
к EA и учитываются отдельно. Это важно для CSR с побочными эффектами.

Для BIC/BIS flags NZV вычисляются из сохранённого результата T2 после
успешного WRITE ACK, C остаётся прежним. Для SUB старый destination
сохраняется в T3, как для ADD; после WRITE повторяется вычисление ALU
для NZVC. Дополнительного flags latch нет. При ошибке записи новое значение
PSW не фиксируется. Такая последовательность проверяется для всех пяти
пишущих word operations.

Микрокодная пауза prefetch и PC-only stream hint не менялись. Старые CP7
benchmarks повторены с теми же retirement, memory beat и SPI counts.
Все семь RR instructions имеют 2 CPI ideal RAM и 39.078125 CPI в FRAM
loop (63 RR + BR). Memory workloads измерены отдельно в [benchmarks.md](benchmarks.md).

## Проверки

**12928 RR/BR cases** содержат прежние 6272 cases без изменения порядка,
затем новые edge/alias/random cases для BIT/BIC/BIS/SUB. Для EA oracle
перебирает 7×8×8×8×8 mode/register combinations, memory/immediate flag edges
и indexed wrap. Из **32298** кандидатов **31671** завершаются в принятом
RAM/FRAM subset. **627** исключены явно: 595 abort и 32 I/O trace cases;
эти исключения не считаются реализацией traps или периферийного oracle.

Все **7×8×8 mode pairs** покрыты. Сравниваются R0..R7, PSW и точный порядок,
адрес, данные и направление **137006 bus beats**. RAM с 0..3 waits:
948247 clocks; FRAM: 14739672 clocks. На каждой памяти portable и vendor
ROM дают одинаковые результаты и побайтно одинаковые cycle CSV.

14 directed cases проверяют семь I/O operations, нечётные source/pointer
и пять write errors. 4096 datapath checks перебирают пары и RF selectors;
отдельно проверяется single-cycle BA SUB/BIC writeback. Прежние ALU/flags,
sequencer, prefetch и integration tests с периферией lsi11-fpga сохранены.

Byte ISA, single-operand ISA, conditional branches, traps/interrupts и EIS
ещё не реализованы. При abort частичное состояние registers не заявляется
идентичным DCJ11; source mode2/3 increment выполняется после READ ACK.
MMU и расширенные CPU addresses отсутствуют.

## Resource gate

Сначала синтезировалась пара BA с прежним CP7 microcode: core 600/277/4,
40 MHz PASS, FRAM 850/393/4, 29.56 MHz PASS. Затем семь word instructions:
core **600/277/4**, FRAM **878/393/4**. Counts включают измерительную обвязку.
Итоговый FRAM проходит 29.56 MHz, Fmax 31.540 MHz; относительно CP7 +17 LUT,
FF и EBR без изменения. Это ещё не полный board top.

Core при 40 MHz имеет Fmax 39.955 MHz, FAIL на 0.028 ns к PSW Z. Новый
placement при 39 MHz дал Fmax 38.724 MHz и тоже FAIL. Сохранённый NCD из
40 MHz run проверен отдельным TRACE при 39 MHz: PASS, margin 0.613 ns.
Такой результат относится к этому размещению, не обещает 39 MHz после
любого нового MAP/PAR. [Reports и воспроизведение](synthesis.md).
