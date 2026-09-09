# CP14: IRQ, WAIT, SPL и периферийный адаптер

Профиль остаётся CM=PM=RS=T=0, один kernel register set, адрес 16 бит,
без MMU. Добавлены resolved IRQ, WAIT, SPL 0..7 и reset PSW=000340 octal.
HALT/RTT/trace, architectural bus/illegal traps, остальные PSW/system
instructions и register banking пока отсутствуют.

Первичные локальные источники: `core/core.c` (`irq_accept`, `core_step`,
`core_reset`, SPL), `lsi11/dev_kw11.c`, `lsi11/dev_dl11.c` и замороженные
[AM4 board bus](../reference/lsi11/am4_cpu11_bus.v),
[UART](../reference/lsi11/wbc_uart_xo2.v). [Hashes источников](irq-source-audit.json). Оригинальный emulator и sibling
периферия не изменены. HALT у DCJ11 oracle вызывает отдельный console path;
простой diagnostic STOP не объявляется его реализацией.

## Контракт IRQ

Core и FRAM wrapper принимают синхронные `irq_valid`, `irq_priority[2:0]`,
`irq_vector[8:1]` и выдают `irq_ack`, `waiting`. Priority 1..7 допускается
только при `priority > PSW[7:5]`; level0 не принимается. Вектор — младшие
девять бит byte address, bit0 всегда нулевой. Проверены все 255 ненулевых
чётных векторов; vector0 отдельно не проверен.

Источник удерживает запрос до ACK, арбитр предоставляет выбранные priority
и vector стабильными вокруг принимающего фронта. Более высокий запрос может
изменить выбор арбитра до принятия. `irq_ack` — комбинационный clock-enable,
принимаемый на rising edge; это подтверждение принятия запроса, а не успешного
завершения stack frame. После ACK входной vector может изменяться.

IRQ проверяется на завершающей ALU microinstruction либо в WAIT. Он не
прерывает начатый READ/WRITE и не добавляет постоянного такта обычной ISA.
Флаги/RF завершающейся инструкции фиксируются, затем начинается IRQ frame.
При failed/odd FETCH или operand transfer architectural commit и IRQ ACK
подавлены; остаётся прежний diagnostic STOP.

IRQ сохраняет vector в уже существующем MDR и переходит на 013. Две
микрокоманды переносят MDR→T4 и вызывают общий CP13 frame 3d6..3e3.
Не добавлены vector register, PC history, третий RF read port или trap FSM.
Один `irq_active` FF подавляет фиктивный `retire` при завершении IRQ frame;
инструкция до IRQ уже получила свой единственный retire. Более высокий IRQ
может приниматься после завершения frame, до первого opcode обработчика.

WAIT — control command14 в encoding v8, entry012. Он удерживает uPC и
запрещает новые speculative launches. `wait_seen` FF даёт один retire при
входе в WAIT, без повторных pulses при ожидании. Допустимый IRQ будит core;
RTI возвращает PC за WAIT. `waiting` отражает WAIT microinstruction, включая
такт её первого выполнения. Reset очищает ожидание и IRQ state.

IPL проверяется **до фронта** текущей микрокоманды. Поэтому PSW LOAD,
меняющий IPL, должен предшествовать terminal ALU word. SPL делает отдельный
финальный NOP/FETCH, RTI загружает PSW перед PC commit. PSW LOAD совместно
с ALU seq=FETCH/FETCH_A1 не используется в production microcode; значение IPL
того же фронта не обходится через ALU в IRQ comparator.

## SPL и microstore

Decoder напрямую использует IR[2:0] для entry1f0+2×level. Восемь двухсловных
stubs создают constant level<<5 и переходят на общий хвост 3e4..3e8:
mask FF1F через NOT, AND PSW, OR нового IPL, LOAD, затем FETCH.
NZVC и остальные PSW bits сохраняются. Kernel-only профиль проверяется
oracle; режимы вне него не объявляются совместимыми.

Всего **446/1024 words**, 43.5546875%, 36 bits, 4 EBR; +24 words к CP13.
Все 422 прежних words и labels сохранены. Encoding v8 добавляет WAIT;
ALU field placement и ранний `FETCH_A1` остаются прежними. SPL — 8 CPI ideal
RAM. WAIT без ожидания — 2 CPI до первого retire; IRQ frame — ещё 16 clocks
при ideal RAM, его выполнение не считается PDP-11 instruction.

## KW11/KL11

[`uj11_irq_lsi11`](../rtl/uj11_irq_lsi11.v) фиксирует одноклоковый `event_irq`
в одном pending FF. KW11-L получает BR6/vector0100, UART — BR4 и уже
разрешённый vector060/064. Timer выше UART; внутри UART RX выше TX.
Одновременный новый tick и timer ACK оставляют pending установленным.
Несколько ticks до ACK объединяются в одну заявку; это не счётчик событий.

`uart_ack` передаётся в legacy `interrupt_strobe` только при принятии UART.
Адаптер рассчитан на UART request; общий legacy `virq` содержит также RK
service/hardware requests, поэтому подключать его без разделения RK нельзя.
В интеграционном тесте RK service отключён и RK registers не используются.
RK service protocol остаётся отдельным будущим gate.

Новый testbench запускает CPU и все vectors/frames через настоящую SPI FRAM,
а CSR UART/таймера обслуживает замороженный board bus. Проверяются три
одновременно pending источника (timer→RX→TX), RX byte sign extension,
импульс таймера внутри FRAM READ/WRITE, masked WAIT, wake/RTI и reset.
После стабилизации WAIT не выдаёт memory requests, новые SPI transactions
или повторные retire. Пять сценариев, шесть проверок установившегося состояния.

## Измерения HC1200

| Gate / scope | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---:|---:|---:|---|---:|
| CP14a/d core + generic IRQ + probe | 764 | 293 | 4 | 35 MHz PASS | 36.720 MHz |
| CP14b FRAM/prefetch + generic IRQ + probe | 1038 | 410 | 4 | 29.56 MHz PASS | 30.992 MHz |
| CP14c FRAM/prefetch + KW11/KL11 IRQ adapter + probe | 1016 | 410 | 4 | 29.56 MHz PASS | 31.221 MHz |

Generic IRQ scope относительно CP13: core +29 LUT, FRAM +39 LUT. Реальное
core state выросло на два FF; ещё 14 FF — новые независимые входы и наблюдения
probe. CP14c ограничивает источники BR4/BR6 и имеет другую probe: 29 stimulus +
179 observation FF; рабочий state добавляет также timer pending. Его меньший
LUT count не означает, что адаптер имеет отрицательную стоимость: synthesis
использует ограниченный набор значений priority и другой boundary circuit.
Полные UART/timer/panel/SD/RK и board pin delays в эти fits не входят.

Critical path CP14c: EBR→selector/D→ALU→writeback→prefetch redirected,
31.691 ns, 22 logic levels, slack 1.799 ns. IRQ не стал худшим путём.
CP14d: EBR→datapath→probe observation, 27.508 ns, 14 levels.
Начальная цель 50 MHz по-прежнему не достигнута.

## Проверки и воспроизведение

Новые **6654 завершённых DCJ11 cases, 0 exclusions**: все IPL/priority и
NZVC для MOV/WAIT/обеих ветвей SOB, все SPL levels, IRQ после SPL/RTI,
все ненулевые чётные vectors. Oracle предлагает один IRQ на instruction;
ожидаемый vector-entry факт и exact bus trace проверяются явно.
18678 bus beats; 101315 RAM clocks при 0..3 waits; 2160254 FRAM clocks.
Позднее поступление IRQ и многократные запросы проверяются отдельно directed
тестом периферии и nested IRQ benchmark.

104 directed core cases проверяют ошибки обоих vector reads, обоих frame
writes, odd SP, смену внешнего vector сразу после ACK и приоритет ошибки
instruction fetch над IRQ. Они фиксируют terminal fault policy, без заявления
полной DCJ11 abort compatibility. Adapter: 763 проверки, включая 500 clocks
удержания pulse и simultaneous tick/ACK.

Полная регрессия и неизменность CP13 проверяются командой `make verify-cp14`:
[verification manifest](verification-cp14.json), [benchmarks](benchmarks-cp14.json).
Результат gate принимается только после завершения recorder. FPGA не программировалась.

Следующий gate: architectural illegal/bus traps с очисткой EA CALL state и
защитой от ошибки внутри frame. Trace/RTT и HALT требуют отдельных правил;
они не добавляются как aliases к RTI/STOP. EIS следует после рабочей Stage 2.
