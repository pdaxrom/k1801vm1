# CP20: HALT restart profile и RESET

CP20 добавляет только два opcode: `000000` и `000005` octal. Все адреса
16-bit logical == physical, MMU отсутствует. Сохранены RF16×16, Q, ALU,
PSW, sequencer, memory transport и prefetch. Один kernel register set,
CM=PM=RS=0, NZVC/IPL/T; register-bank/processor-mode exchange не реализован.

## Граница совместимости HALT

Первичный oracle — существующий [`handle_halt` и opcode switch в core.c](../../core/core.c),
сборка DCJ11, ENABLE_MMU=0. В этом проекте kernel HALT выполняет restart:

1. SP−=2, WRITE старого PSW.
2. SP−=2, WRITE PC за HALT.
3. READ слова `000004`, PC=прочитанное & `0177776`.
4. PSW=`000340` (IPL7, T/NZVC=0).

Слово `000006` не читается. Это отличается от common trap, который сначала
читает vector+2 и vector, затем строит frame. HALT не устанавливает `stopped`.
Если T был установлен при FETCH, существующий oracle затем выполняет trace
014; новый IPL7 маскирует обычные pending IRQ. Эти случаи проверены отдельно.

Это **профиль restart существующего эмулятора**, а не полная реализация
console ODT физического DCJ11. У чипа действие HALT зависит также от режима
и halt option: возможны console ODT или trap004 с CPU error.
[DEC DCJ11 User's Guide, §6.3.7](https://www.bitsavers.org/pdf/dec/pdp11/1173/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf).
ODT, halt-option straps, non-kernel HALT и CPUERR.HALT в CP20 отсутствуют.
Код, которому нужен именно console monitor, пока нельзя считать совместимым.

Ошибка внутри любого из трёх HALT frame transfers даёт существующий terminal
STOP: fault_code=2 при ACK+error, =1 при odd word. Неуспешная запись не
коммитится; уже выполненные SP decrements и предыдущие writes сохраняются.
Это явно заданная double-fault policy uJ11, проверенная directed tests;
совпадение с abort behavior HALT в C-эмуляторе не заявлено.

## RESET и настоящая периферия lsi11-fpga

`peripheral_reset` — новый выход engine/core/fram_system. Микрокоманда JUMP
с `init=1` фиксирует его в отдельном FF на следующий clock — settling word.
Затем импульс снимается, и terminal ALU word проверяет trace/IRQ. Выход
зарегистрирован: KL11 UART имеет asynchronous reset, поэтому комбинационный
decode нескольких ROM bits нельзя подавать непосредственно на его reset. RESET не пишет RF, Q или PSW,
не сбрасывает CPU, FRAM transport, память или весь instruction stream.
Предшествующая обычная READ/WRITE завершена до входа в RESET. Уже начатый
спекулятивный FRAM READ может завершиться независимо от peripheral pulse.

| Исполняемый uPC | peripheral_reset перед фронтом | Действие на фронте |
|---|---:|---|
| 021 JUMP.init | 0 | Регистр выхода принимает 1, переход на 022 |
| 022 settling ALU | 1 | Синхронная периферия и IRQ latch сбрасываются; выход принимает 0 |
| 023 terminal ALU | 0 | Retirement, trace/IRQ после сброса периферии |

Async reset UART срабатывает при подъёме зарегистрированного выхода после
фронта 021; он не зависит от переходных значений комбинационного ROM decode.

Подключение синхронной board integration:

```verilog
// uj11_fram_system.peripheral_reset -> init
// am4_hc1200_cpu11_bus.peripheral_reset -> init
// uj11_irq_lsi11.reset -> board_reset || init
// CPU и uJ11 FRAM transport получают только board_reset.
```

В [integration test](../tb/tb_system_control_lsi11.v) использован замороженный
[`am4_cpu11_bus.v`](../reference/lsi11/am4_cpu11_bus.v), настоящий KL11 UART,
KW11 timer и SD byte service; sibling working tree не менялся. Три сценария
проверяют pending IRQ до исполнения и tick во время FRAM READ/WRITE.
После RESET очищены UART RX/TX requests, KW11 pending/IE, panel latch,
SD CS/SCK, RK control/service state. Данные CPU FRAM и регистры сохраняются,
новый KW11 IRQ после RESET снова будит WAIT и возвращается через RTI.

RK CS1 в исходной периферии физически хранится во втором FRAM bank. Для
настройки его IE testbench использует отдельную модель private peripheral
FRAM через manual CSR cycle. CPU instructions/operands/vectors/stack идут
через uJ11 FRAM; тест запрещает попадание CPU traffic в private peripheral
FRAM. Это проверка RESET control state, не выполнение RK ROM service/DMA.

RESET callback существующего C по умолчанию пуст. Differential harness
проверяет два явных варианта external IRQ: источник остаётся asserted либо
очищается callback. Настоящий board adapter проверен отдельно и очищает
свой pending latch вместе с периферией. PIRQ register не реализован.
Выход `init` рассчитан на синхронную FPGA-периферию; длительность физических
Qbus INIT/GP strobes не эмулируется. Полный board top ещё предстоит.

## Микрокод v11

Ширина 36 bits и глубина 1024 words сохранены. Новый context: control JUMP
bit0=`init`. Только JUMP допускает это поле; assembler проверяет 0/1 и требует
`prefetch=0` при init=1. Остальные control fields и ALU bit0 (`trace=RETURN`
или immediate context) неизменны. Новых command codes нет. Добавлен один FF выхода peripheral_reset.

| Entry | Адреса hex | Words | Назначение |
|---|---|---:|---|
| HALT_RESTART | 018 | 1 | TRAP frame guard и переход |
| HALT_ENTRY | 3e9–3f2 | 10 | PSW/PC push, READ004, mask PC, force PSW |
| RESET_BUS / RESET_SETTLE | 021–023 | 3 | Pulse, release/settle, retirement |

Всего **521/1024 words**, +14 относительно CP19. Все прежние 507 words и
labels сохранены побайтно/по адресам. Exhaustive decoder miter проверяет
все 65536 encodings: меняются только HALT и RESET; supported=56432.
Программист использует `JUMP, target=RESET_SETTLE, init=1, prefetch=0`;
ручных hex ROM edits нет. ROM/RTL/assembler v11 обновляются вместе.

## Реальные synthesis gates

Diamond 3.14.0.75.2 / Synplify, LCMXO2-1200HC-4SG32C, 2026-09-09.
Приняты CP20c/d: оба scope прошли MAP/PAR/TRACE, все nets routed, EBR=4.

| Revision | Features | LUT4 | FF | EBR | Constraint | TRACE MHz | Words |
|---|---|---:|---:|---:|---|---:|---:|
| CP19a | Core + probe | 844 | 297 | 4 | 35 MHz PASS | 36.426 | 507 |
| CP20a | Candidate: combinational init | 832 | 298 | 4 | 35 MHz PASS | 36.516 | 521 |
| CP19b | FRAM/prefetch/IRQ + probe | 1073 | 414 | 4 | 29.56 MHz PASS | 30.046 | 507 |
| CP20b | Candidate: combinational init | 1089 | 415 | 4 | 29.56 MHz PASS | 31.010 | 521 |
| **CP20c** | **Registered init, core + probe** | **836** | **299** | **4** | **35 MHz PASS** | **36.647** | **521** |
| **CP20d** | **Registered init, FRAM/IRQ + probe** | **1085** | **416** | **4** | **29.56 MHz PASS** | **30.593** | **521** |

FF+2 относительно CP19: один register выхода RESET и один observation FF.
Исходные CP20a/b прошли functional tests, но не приняты: комбинационный init
не подходит для asynchronous reset входа actual UART. Регистрация использует
предусмотренный settling word, ISA/memory CPI не меняются. CP20c/d повторно
прошли полную регрессию с portable и vendor ROM.

Изменение LUT включает оптимизацию всей схемы и ROM; −8 core / +12 FRAM к
CP19 нельзя приписать локальной цене одной инструкции. CP20d critical path:
EBR→prefetch.redirected FF, 32.348 ns, 20 logic levels, 53.5% route.
Запас — 195 LUT до физических 1280, 15 до желательных 1100. Ресурсы полного
UART/timer/panel/SD/RK и board pin timing в fit не входят; 50 MHz не достигнуты.
[cp20c reports](../synth/reports/cp20c/result.json),
[cp20d reports](../synth/reports/cp20d/result.json).

## Сопоставление с предыдущими экспериментами

Исходные отчёты и различия scope сохранены в
[previous_experiments.md](previous_experiments.md). Исторический AM4 FRAM board:
1058 LUT /393 FF /7 EBR, TRACE 30.469 MHz. Stable microcpu ucode с SD/FIS/RT-11:
1095/431/7, TRACE 37.627 MHz. CP20d FRAM/resolver/probe:1085/416/4, TRACE 30.593 MHz.

У uJ11 заняты четыре EBR, но его ISA и board scope пока меньше: EIS, banking,
полный SD/RK boot и запуск ОС ещё не реализованы. Эти числа не доказывают
равную полноту или преимущество всей системы. Сопоставимых отдельных
HALT/RESET CPI для исторических AM4/microcpu в сохранённых данных нет;
ускорение этих инструкций относительно них не вычисляется. Для текущего
изменения сопоставимы именно CP19a/b и CP20c/d при одинаковых constraints.

## Проверки и измерения

6144 новых completed DCJ11 fixtures:256 PSW ×8 IRQ priorities ×
(HALT, RESET с retained IRQ, RESET с cleared IRQ). Сравниваются все registers,
PSW, PC, точный порядок/read-write data bus, trace и IRQ, число RESET callbacks.
Для HALT restart values покрывают odd/even, low/upper addresses; 006 заполнен
отличающимся значением. **Новых exclusions нет**. RAM:137216 microclocks;
FRAM:3055232; обе модели:26368 exact logical bus beats.

Полная регрессия:176281 completed C cases на каждом RAM/FRAM×portable/vendor
ROM сочетании, включая все 170137 прежних. Отдельно 33788 прежних fault frames,
64 новых directed HALT fault frames (T=0/1 и eligible IRQ при IPL0)
и три actual-peripheral RESET scenarios, включая сохранение ненулевого Q.
Прежние documented exclusions остаются; они не названы совместимыми.
Шесть mutation controls обязаны провалиться: отсутствующий RESET, незарегистрированный
импульс (не на settling word), потерянный
pulse, HALT через common trap, потерянное PC alignment, неправильный PSW.

| Loop | Ideal RAM CPI | Legacy FRAM | Sequential FRAM | Prefetch FRAM | Logical beats/instruction |
|---|---:|---:|---:|---:|---:|
| 31 RESET + BR | 3.9375 | 108.9375 | 43.0625 | 43.0625 | 1 |
| HALT; RTI; BR | 7.333333 | 298.666667 | 298.666667 | 298.666667 | 2.666667 |

Warmup исключён, RESET loop измерен 8 раз (256 retirements), HALT loop64 раза
(192 retirements); HALT handler содержит RTI и восстанавливает SP/PSW.
Отдельный RESET с ideal FETCH — 4 clocks, HALT — 12; loop CPI включает RTI/BR.
Всего 794 benchmark runs на ROM-модель, все 786 прежних counts сохранены.
При nominal29.56 MHz новые FRAM loops дают рассчитанные~0.686/~0.099 MIPS;
это functional RTL simulation, не измерение платы.

Воспроизведение: `make verify-cp20`. Он запускает portable tests и snapshots,
затем vendor tests, сравнение результатов и audit архивов. Vendor DP8KC/GSR/PUR
задаются через LATTICE_SIM_DIR. [Manifest](verification-cp20.json),
[сырые cycle/benchmark данные](benchmarks-cp20.json).

Следующий отдельный gate — EIS через ALU/Q и микрокодные циклы с повторным
HC1200 fit. До него нужно учитывать малый LUT запас; полнота EIS, banking,
ODT и полный board top не заявлены. MMU не проектируется и не резервируется.
