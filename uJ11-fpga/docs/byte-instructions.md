# CP10: byte ISA

Реализованы MOVB/CMPB/BITB/BICB/BISB и CLRB/COMB/INCB/DECB/NEGB/ADCB/
SBCB/TSTB/RORB/ROLB/ASRB/ASLB во всех восьми addressing modes. Word ISA
и все 15 branch classes сохранены. MMU отсутствует, CPU address строго 16-bit.

| Final scope | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---:|---:|---:|---|---:|
| CP10j core + probe | 659 | 277 | 4 | 35 MHz PASS | 36.496 MHz |
| CP10k core + FRAM/prefetch + probe | 905 | 394 | 4 | 29.56 MHz PASS | 32.047 MHz |

342/1024×36 microinstructions, encoding v5. Full peripheral board top и pin
timing не входят в scope. Прежние 40 MHz у отдельного core пока не достигнуты;
50 MHz остаётся целью. Все промежуточные failures сохранены в [synthesis.md](synthesis.md).

## Datapath и адресация

Один 17-bit adder обслуживает word/byte. Carry через bit7 восстанавливается
из `sum[8] xor a[8] xor arithmetic_b[8]`; второй adder не добавлен.
N/Z/V/C выбирают bit7/15. Byte right shifts вводят sign/carry в bit7.

Destination OPERAND сохраняет RF[B][15:8], MOV знаково расширяет low byte;
при word оба пишут весь result. Writeback содержит полное итоговое значение,
включая PC, чтобы prefetch видел правильный redirect. RF остаётся 16×16 с
двумя чтениями/одной записью. Q и количество core state FF не изменились.

Адресные/temporary transfers с dst=RF, flags=KEEP остаются word. Modes 2/4
изменяют R0–R5 на 1 при byte, R6/R7 на 2. Deferred modes 3/5 меняют любой
регистр на 2 и читают word pointer; displacement/pointer всегда word с
odd-word guard. Immediate (PC)+ возвращает byte, увеличивая PC на 2.
Source mode0 при memory destination считывается после EA updates, согласно
существующему DCJ11 oracle; CAPTURE_REGISTER_SOURCE routine сохранена.

Control `byte=IR` выбирает final operand width. Pointer/extension READs
остаются word. MOVB в memory пишет один byte, CMPB/BITB/TSTB не пишут,
CLRB/MOVB не читают конечный destination. NZVC фиксируются после WRITE ACK.

## FRAM, prefetch и CSR

Реальная память — SPI FRAM MR45V100A. Byte demand передаёт один byte по
точному адресу; high/low lane преобразуется на границе legacy peripheral bus.
Aligned byte instruction stream внутри читает целое слово: opcode/immediate
могут пользоваться одним retained READ. Core видит только low byte, next
prefetch address продвигается на 2. Для data/CSR speculation запрещена.
Destination/unary mode2 PC сейчас использует обычный demand path; отдельного
ускорения этого редкого случая не добавлено.

После byte merge critical path проходил ALU→PC compare→valid control.
Принят один FF `redirected`: он маскирует prediction/buffer valid сразу после
PC-write edge, stored bits очищаются следующим clock. Matching PC сохраняет
буфер; FETCH increment исключён. Нового execution cycle не требуется.
FRAM fit уменьшился с failed 934/393/4 до 905/394/4, Fmax 29.402→32.047 MHz.

## Verification и границы oracle

Источник semantics — существующий `../core/core.c`, DCJ11, ENABLE_MMU=0.
Проверяются все registers, PSW, PC, порядок и ширина bus beats, read/write
values; byte writes сохраняют соседний byte. Для RAM применены 0..3 waits;
для FRAM — настоящий RTL SPI transport и memory model.

* 2097152 byte ALU result/NZVC checks: все low-byte пары × C × 16 ALU ops,
  с независимыми high bytes. 16384 merge/sign-extension/word fallback checks
  по всем 16 RF destinations, включая PC. Старые word tests сохранены.
* 23954 завершённых double-byte cases из 24470 кандидатов: все 5×8×8 mode
  combinations, Rs/Rd aliases, edge values, SP/PC, immediate/absolute/indexed.
  98606 exact demand beats, 683191 RAM / 9773277 FRAM clocks.
* Исключены 516 cases: 470 abort, 60 I/O, с overlap 14. Internal DCJ11 CSRs
  могут обходить bus callbacks, поэтому сборочная копия core.c содержит
  четыре read-only address hooks на входах load/store. Инструмент проверяет
  обратное преобразование; исходный emulator и ISA semantics не изменены.
  Все обращения к I/O исключаются до Stage 2/system-register implementation.
* 7104 unary byte cases, без exclusions: 12×8 modes и NZVC edge combinations;
  19680 demand beats, 99696 RAM / 1884432 FRAM clocks.
* 23 double-byte и 46 unary-byte directed cases: even/odd CSR, read/write
  errors, odd-word pointer rejection, PSW commit и request stability.
* 525 prefetch protocol beats, включая matching PC и mismatch каждого из
  16 bits; 19 beats с замороженной настоящей периферией lsi11-fpga.
* Полный decoder: все 65536 encodings, 54528 accepted. 11 Python methods,
  word ALU/RF/Q/sequencer/memory tests и Verilator --Wall без предупреждений.

Итого 90177 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM.
CSV и benchmark JSON совпадают побайтно. Все прежние 430 benchmark runs
сохраняют counts, добавлены 176 byte runs (44 workloads × 4 memory modes).
[Verification](verification-cp10.json) · [Benchmarks](benchmarks-cp10.json).

Это функциональная simulation и FPGA synthesis, не запуск физической платы.
Abort partial register state, architectural trap frames, internal DCJ11 CSR,
privilege/banking и EIS пока не являются заявлением совместимости.

Воспроизведение: `make test`; затем vendor targets из README. Для сверки
сохранить portable CSV/JSON с суффиксом `-portable` до vendor run и запустить
`python3 tools/record_cp10.py`. Исторические manifests не перезаписываются.
