# Stage 2: исходный план и выполненные CP13–CP18

**Актуальное состояние после CP20:** выполнены MFPS/MTPS (CP19) и HALT
restart/peripheral RESET (CP20). HALT — профиль существующего эмулятора,
не native console ODT. Следующий отдельный gate — EIS на ALU/Q и microcode loops;
до расширения datapath снова нужен HC1200 synthesis. Baseline FRAM1085 LUT,
416 FF,4 EBR,29.56 MHz PASS. [Контракт CP20](system-control.md).
Остальной текст ниже — исторический план; banking/ODT/board integration
остаются отдельными нерешёнными задачами. MMU не входит в эти gates.


Первый шаг выполнен: [CP13 software traps / RTI](software-traps.md).
Второй шаг выполнен: [CP14 IRQ/WAIT/SPL](interrupts.md).
Третий шаг выполнен: [CP15 reserved/invalid-mode traps](reserved-traps.md).
Четвёртый шаг выполнен: [CP16 memory bus/address traps](memory-faults.md).
Пятый шаг выполнен: [CP17 trace/RTT](trace-rtt.md).
Шестой шаг выполнен: [CP18 CC/NOP/MFPT](system-flags.md).
Ближайший gate — MFPS/MTPS с byte EA и IRQ после обновления IPL.
Ниже сохранён исходный план CP12. Далее нужны HALT/RESET и
оставшиеся PSW operations, с отдельным synthesis gate для каждого изменения.
CP12 заканчивает текущий basic integer subset; в нём ошибки всё ещё дают
diagnostic STOP, а HALT/WAIT/RTI/RTT и architectural vector handling отсутствуют.
Первая версия остаётся без MMU и с 16-bit logical == physical address.

## Первый ограниченный шаг — выполнен в CP13

Начать с BPT/IOT/EMT/TRAP и RTI в явно ограниченном профиле: один kernel
register set, CM=PM=0, RS=0, T=0. Сначала измерить common vector entry/return
microcode на том же datapath; затем добавлять asynchronous arbitration,
trace semantics и расширение PSW. Это промежуточный gate, не полная Stage 2
и не обещание J-11 privilege compatibility. Unsupported mode/RS/T state
должно распознаваться в verification и документироваться, а не молча считаться
работающим. Активный RF остаётся 16×16; его расширение ради banking не планируется.

Прямой software dispatch в microcode позволяет проверить stack frame без
нового аппаратного trap FSM. Текущие D=PSW/MDR/IMM, RF temporaries,
READ/WRITE и PSW LOAD уже выражают основные операции. Количество занятых
words и ресурсов заранее не заявляется: сначала assembler/testbench и отдельный
core/FRAM synthesis gate, затем решение о принятии.

## Порядок операций oracle

Первичные исходники: [`core_take_vector`, RTI/RTT и `irq_accept`](../../core/core.c).
У model DCJ11 `core_take_vector` выполняет:

1. READ vector+2 (новый PSW), затем READ vector (новый PC).
2. Обновление PSW с CM=kernel и PM=старый CM; при полном J-11 здесь возможен
   register-set/stack-mode exchange. В первом kernel-only gate этого exchange нет.
3. SP−=2, WRITE old PSW; SP−=2, WRITE old PC; PC=new PC.

Векторы octal: BPT/trace 014, IOT 020, EMT 030, TRAP 034, illegal 010,
bus/address 004. После успешного frame [SP]=old PC, [SP+2]=old PSW.
Software trap сохраняет PC за opcode. RTI снимает PC, затем PSW; при каждой
операции нужен отдельный READ ACK. RTI и RTT различаются trace behavior:
DCJ11 RTI проверяет восстановленный T, RTT подавляет trace в этом шаге.
Пока T не реализован, нельзя объявлять RTI/RTT полностью совместимыми.

Нужно отдельно выбрать и проверить поведение ошибки внутри vector fetch/push:
recursive trap entry без защиты недопустим. Существующий diagnostic STOP
можно сохранить как явный terminal double-fault outcome первого gate.
Полная DCJ11 red/yellow stack-limit architecture — отдельное расширение.

## Что потребуется для bus faults и interrupts

В исходном CP12 `uj11_engine` bus_fault запрещает `step` и фиксирует stopped.
Поэтому CJUMP bus_error сам по себе не превращает ошибку в microcoded trap:
sequencer тоже остановлен. Следующий аппаратный gate должен различать
architectural commit и перенаправление ROM/uPC на fault entry, сохраняя
запрет RF/Q/PSW/MDR side effects неуспешной транзакции. Fault внутри EA CALL
также требует очистить занятый return slot; иначе trap routine получит ложный
CALL overflow. Эти изменения нельзя скрыть только заменой ROM.

IRQ принимается на instruction boundary, после commit. Нужны приоритет по
PSW[7:5], однократный acknowledge и запрет speculative fetch на entry.
Длинная FRAM операция должна либо завершиться, либо получить error; IRQ
не отменяет уже начатый WRITE и не теряет его ACK. При одновременных fault,
trace, IRQ и WAIT нужен явный проверяемый порядок приоритетов. Скорость
обычного RR path необходимо сравнить с CP12, чтобы boundary check не добавил
постоянный лишний microclock.

## Периферия lsi11-fpga

Изучена замороженная копия [`am4_cpu11_bus.v`](../reference/lsi11/am4_cpu11_bus.v)
и [`wbc_uart_xo2.v`](../reference/lsi11/wbc_uart_xo2.v), использованная в CP6+ tests.

* KL11 даёт latched RX/TX IRQ; vector 060 имеет приоритет над 064.
  Acknowledge очищает выбранную заявку. Это отдельный vector handshake,
  не обычное чтение RAM по 060/064.
* KW11-L выдаёт `event_irq` импульсом в один clock при enabled tick.
  Нужен pending latch до принятия: прямое соединение с boundary-only
  проверкой потеряет события во время медленного SPI READ/WRITE. Vector 100.
* RK hardware IRQ — vector 210. `rk_service_pending` имеет отдельный
  protocol и ROM service entry; его нельзя автоматически объявить обычным
  J-11 interrupt. Сначала UART/timer, затем отдельная RK integration.
* RESET должен сбрасывать peripheral state через отдельный сигнал;
  его нельзя выполнять сбросом core посреди собственной instruction.

Не модифицировать sibling peripheral working tree для этого gate. Полный
board synthesis ещё не выполнен: CP13 FRAM probe оставляет 281 LUT/3 EBR,
это не гарантия, что IRQ+UART+timer+SD/RK поместятся вместе. Ресурсы под MMU
не резервируются.

## Обязательные проверки следующего gate

Новый trap oracle должен сравнивать успешный vector entry, а не пропускать
все `attempted_vector` cases, как текущий ordinary-instruction fixture filter.
Сравнивать exact order READ vector+2/vector, WRITE PSW/PC, registers/PSW,
возврат RTI, nested software traps, 0..3 RAM waits и настоящий FRAM protocol.
Ошибки обоих vector reads, обоих frame writes и обоих RTI reads проверять
отдельно; не выдавать совпадение successful cases за abort compatibility.
Для IRQ — masking, competing sources, one-cycle timer pulse during FRAM,
request retention, WAIT wakeup, pending at retirement и prefetch redirect.
После каждого аппаратного изменения — HC1200 MAP/PAR/TRACE и прежние CPI.


## Уточнения перед следующим gate после CP14

`core.c:illegal_trap` сохраняет текущий R7 и вызывает vector010. Однако
mode0 JMP использует `trap_vector4`, а mode0 JSR у DCJ11 — `illegal_trap`.
Их нельзя одинаково перенаправлять на010 только по факту invalid mode.
Нереализованные пока EIS/system opcodes нужно отличать в verification от
настоящих reserved encodings DCJ11; trap для отсутствующей инструкции не
доказывает полноценную ISA-совместимость.

`bus_error_trap` сохраняет architectural R7 на момент ошибки: у operand
fault это PC после уже выполненных FETCH/extension increments, у fetch fault
PC остаётся на ошибочном opcode. Дополнительный instruction-PC history
register ради этого oracle не нужен. Старые различия partial abort state
(например, момент auto-increment и RTI pop increments) требуют отдельного
разбора, прежде чем заявлять полную fault differential compatibility.

Ближайший ограниченный шаг может сначала добавить reserved/invalid-mode
vector entry через прежний common frame, сохранив diagnostic STOP для
memory faults. Отдельный последующий hardware gate сможет добавить vector004
для bus/address ошибок с commit suppression и очисткой EA CALL link.
Это позволяет измерить цену каждого изменения, не объединяя его с trace,
HALT и будущей ISA. MMU к этим изменениям отношения не имеет.

## Следующий ограниченный шаг после CP18: MFPS/MTPS

Текущий baseline CP18e/f:493 words v10, core843/297/4 при35 MHz PASS,
FRAM/resolver1062/414/4 при29.56 MHz PASS. Сначала исследовать эти две
byte PSW operations на прежнем datapath, затем снова измерить оба scopes.

В первичном `core.c`, MFPS(01067xx) выполняет DECODE_DSTB, захватывает
PSW[7:0], пишет как MOVB (sign extension в регистр), обновляет NZ, очищаетV
и сохраняетC. Регистр назначения должен допускать одну execution ALU
microinstruction с D=PSW/dst=MOV; memory destination — byte WRITE через
общий EA path. Flags должны обновляться после успешного WRITE.

MTPS(01064xx) использует byte operand: low=operand & octal0357,
то есть T не загружается из operand. Итоговый PSW в kernel profile:
(old PSW & octal0177000) | (old PSW & (FLAG_T|FLAG_H)) | low.
Реализовать mask через существующие IMM/ALU/temporary registers; не добавлять
новый PSW write port ради этой инструкции. После LOAD нужен terminal cycle,
как у SPL, чтобы IRQ comparator увидел новый IPL. Non-kernel privilege/mode
behavior не считать реализованным только из-за ширины PSW16.

Oracle должен покрыть все8 modes, R0..R7, byte SP/PC rules, immediate/absolute,
PC-relative, MFPS to PC/SP, all NZVC/T/IPL, IRQ после raise/lower IPL,
operand ACK error/odd indirect word и trace priority. Сравнить точный bus order
с DCJ11; новые abort/I/O exclusions документировать. Не расширять ISA дальше
этих двух операций до нового HC1200 fit.

HALT у kernel DCJ11: push PSW, push PC, read vector004 PC с очисткой bit0,
force PSW0340; это не обычный common trap frame и не diagnostic STOP.
RESET должен через отдельный сигнал сбрасывать actual peripheral state
(UART/KW11/SD/RK/service), не собственный core. Нужны отдельная проверка
IRQ-pending clearing и завершение FRAM WRITE перед reset boundary. Эти
пункты изучены, но в CP18 ещё не реализованы.
