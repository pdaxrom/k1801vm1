# CP17: trace и RTT без MMU

CP17 добавляет T-bit tracing и инструкцию RTT к измеренному CP16. Адрес остаётся
16-битным, logical == physical. Поддерживается один kernel register set:
CM=PM=RS=0; работают NZVC, IPL и T. Переключение processor modes и регистровых
банков не входит в этот этап.

## Порядок событий

Первичный источник для differential testing — неизменённый
[`core/core.c`](../../core/core.c): `core_step`, RTI/RTT, WAIT и `core_take_vector`.

| Событие | Как определяется trace |
|---|---|
| Обычная инструкция | По T перед инструкцией, сохранённому при успешном FETCH |
| Software/reserved trap | По T перед opcode; после его frame возможен ещё один frame vector014 |
| RTI | По восстановленному из стека T; trace предшествует следующему opcode |
| RTT | Для самого возврата trace подавляется; следующая инструкция использует восстановленный T |
| Первый такт исполнения WAIT | Trace пропускается; допустим IRQ |
| Последующее ожидание WAIT | T вызывает trace с приоритетом над IRQ |
| Неудачный FETCH/operand | Приоритет у memory fault; неуспешная операция не делает commit |
| Окончание IRQ/fault/trace frame | Сам frame не вызывает trace; T будет сохранён при FETCH инструкции обработчика |

Обычный trace сохраняет уже обновлённые PC и PSW. Vector014 использует тот же
микрокод frame, что BPT: READ vector+2, READ vector, LOAD PSW, PUSH старого PSW,
PUSH старого PC, установка нового PC. Trace frame не создаёт дополнительный
`retire`. Pending IRQ ждёт исполнения первой инструкции trace handler; ранее
проверенный immediate nested IRQ после IRQ frame сохраняется.

## Минимальная реализация

Добавлен только один state FF — `trace_latched`. Существующее двухбитное
`irq_active` получает значение 3 для trace frame; 0, 1 и 2 сохраняют значения
обычной инструкции, IRQ frame и memory-fault frame. RF16×16, Q, ALU, PSW,
FRAM transport и prefetch не изменены. Нового trap FSM или копии PC нет.

Микросеквенсор выбирает прежний BPT entry 024 при trace. Микрокод остаётся
**452/1024 × 36 bits**: единственное изменение ROM относительно CP16 — bit0
по адресу 037, `trace=RETURN` на завершающей ALU микрокоманде RTI/RTT.
Оба opcode направляются на entry 031; IR[2] различает RTI и RTT.
Ассемблер v10 разрешает этот признак только при `seq=FETCH`, `flags=KEEP`
и D≠IMM. Для D=IMM младший байт сохраняет прежний смысл константы.

Exhaustive decoder comparison проверяет все 65536 encodings: изменился только
000006 (RTT); число поддержанных encodings выросло с 56268 до 56269.
Все **452** прежних адреса микрокоманд и все label addresses сохранены;
451 word побитно совпадает с CP16.

## Проверка

[`trace_bit_vectors.c`](../tb/trace_bit_vectors.c) вызывает настоящий DCJ11 core.
Read-only hooks фиксируют обращения к памяти и vector entry. ISA и flags не
вычисляются отдельной копией emulator. Проверяются точные R0..R7, PSW и все
memory READ/WRITE beats, включая байтовые операции и последовательность frame.

Из 11472 кандидатов завершились **11196 cases**, по 58965 bus beats на каждый
RAM/FRAM прогон. В suite входят все комбинации NZVC/T/IPL для RR/SPL, все
unary operations и addressing modes, byte/word double-operand mode pairs,
branch conditions, RTI/RTT, software traps и WAIT. WAIT с отложенным trace
исполняет два C `core_step`: opcode и ожидание; это один PDP-11 instruction case.

**276 исключений** перечислены в CSV с initial registers/opcode/PSW: abort=220,
vector mismatch=206, I/O=62, odd external word=0; причины пересекаются.
Каждый исключённый кандидат имеет abort или I/O. Неожиданное расхождение
vector без этих причин завершает генератор ошибкой. Исключения не считаются
проверенной совместимостью. Полная fault differential regression CP16 остаётся
в профиле T=0; приоритет fault при T=1 проверен directed tests ниже.

У исходного C emulator после принятия IRQ непосредственно из WAIT остаётся
внутренний `fWait=1` (224 cases). Этот сырой флаг сохраняется в fixture.
Сравнение заканчивается по выполненному IRQ frame; оно не требует от uJ11
продолжать ожидание внутри уже вызванного обработчика. Архитектурные регистры,
PSW и bus trace не исправляются. Выход аппаратного WAIT и исполнение handler
проверены отдельным тестом; исходный C файл не изменён.

Дополнительные проверки:

* 24 сценария `uj11_fram_system`: trace→handler EA CALL→IRQ→WAIT, RTT и следующая
  инструкция, RTI с T=1, поздний IRQ в WAIT, vector PSW с T=1, I/O error с T=1.
* 140 сценариев trace/RTI frame и fault priority при T=1: ошибки всех четырёх
  vector/stack transfers, odd SP, 0..3 waits, отсутствие recursive frame.
* Четыре отрицательных контроля: архивный CP16 без trace и три одиночные
  мутации CP17 (старый T в RTI, trace в RTT, одновременный IRQ acknowledge).
* Все 124969 прежних completed-instruction cases и 32780 fault-frame cases
  CP16, с точным сохранением fixture и per-case clocks.

Повторная ошибка внутри trace frame использует тот же terminal STOP, что
CP16. Red/yellow stack recovery и privilege/mode exchange не добавлены.

## Synthesis и скорость

| Измеренный probe | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---:|---:|---:|---|---:|
| CP17a core + generic IRQ | 826 | 297 | 4 | 35 MHz PASS | 36.302 MHz |
| CP17b core + FRAM/prefetch + KW11/KL11 IRQ resolver | 1046 | 414 | 4 | 29.56 MHz PASS | 30.194 MHz |

Относительно CP16: core +17 LUT/+1 FF; FRAM +4 LUT/+1 FF. В FRAM probe
остаются 234 LUT и 3 EBR; это не полный board top. UART/timer/panel/SD/RK,
внешние pin delays и измерения на физической плате сюда не входят.

Критический путь CP17b: EBR→datapath→prefetch PC/tag comparison→`redirected`,
32.780 ns, 21 logic levels, slack 0.710 ns при 29.56 MHz. Запас уменьшился
относительно CP16 (1.692 ns); начальная цель 50 MHz остаётся открытой.
Число LUT в полном fit зависит также от mapping/packing/routing.

Прежние 726 benchmark runs сохраняют counts. Добавлены 20 runs, всего 746
на ROM-модель. RTT с T=0 имеет прежние 8 microclocks RTI при ideal RAM;
trace добавляет 16 clocks frame после boundary, WAIT — ещё один такт ожидания.

| Новый workload | Ideal RAM CPI | FRAM/prefetch CPI | Bus beats/instruction |
|---|---:|---:|---:|
| ADD, trace, RTT; BR, trace, RTT | 13 | 458 | 4 |
| MOV memory, trace, RTT; BR, trace, RTT | 16.25 | 479.5 | 4.25 |
| RTI с восстановленным T, повторный trace | 24 | 793 | 7 |
| WAIT, trace, RTT; BR, trace, RTT | 13.25 | 450.25 | 4 |
| MOV #SP; RTT с T=0 | 12.5 | 204 | 2.5 |

CPI включает frame и ожидание SPI; frame отдельной инструкцией не считается.
На ADD/trace loop prefetch даёт 458 CPI против 450 без speculation: прерванное
чтение следующего stream word имеет цену. Обычные RR loops сохранили 2 CPI
при ideal RAM и 39.078125 CPI с FRAM/prefetch.

[Raw synthesis reports](synthesis.md), [verification manifest](verification-cp17.json),
[benchmark data](benchmarks-cp17.json).
