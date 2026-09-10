# CP47 — SPI FRAM: локальная проверка, synthesis ожидается

**Три кандидата прошли локальные проверки; resource gate пока не выполнен.**
Измеренных LUT/FF/EBR/Fmax для CP47 нет. Автоматическая проверка отклонила
передачу новых исходников на сервер; запрос на семь файлов CP47 отправлен,
подтверждение ещё не получено. Подготовлены изолированное окружение и
synthesis driver. FPGA не программировалась.

Рабочая основа остаётся CP45k: **1302 LUT / 359 FF / 7 EBR / 652 slices**,
MAP FAIL, превышение HC1200 на 22 LUT / 12 slices. Production CP40h,
принятый APR experiment CP43d и физическая плата CP29a не менялись.
Нельзя считать устранённые RTL-регистры измеренной экономией FF или LUT.

## Кандидаты

`tools/build_fram_cp47.py` проверяет hash исходного transport относительно
CP45k и создаёт отдельные копии в `build/cp47-fram/`. Для полного board
используется CP45 `narrow-rom`, а не отклонённые CP46 control changes.
Native CPU, ALU, MMU bridge/APR, firmware, остальные устройства и microcode
**954/1024×36 v12** не меняются.

| Вариант | Изменение | Статус |
|---|---|---|
| `baseline` | Исходный board FRAM transport | Контроль для будущего synthesis |
| `byte-mux` | Два последовательных выбора word/byte вместо четырёх отдельных payload cases | Formal + simulation PASS |
| `shared-rx` | Приёмный shift register совмещён с верхним байтом `rdata` | Formal + simulation PASS по описанному контракту |
| `combined` | Оба преобразования | Также CPU/vendor/bus/cold FB PASS |

В HIGH=5, LOW=6, DATA_LO=7, DATA_HI=8 слово выбирается через
`state[0] ~^ state[1]`, байт — через `state[1]`. Это используется только
в этих четырёх case branches. WREN, READ/WRITE command, bank byte и default
сохраняются отдельно. Conditional mux сохраняет выбранные X/Z bits.

В `shared-rx` каждый SPI rising edge сдвигает MISO в `rdata[15:8]`.
После DATA_LO этот байт копируется в `rdata[7:0]`; byte transfer также
обнуляет high byte. После DATA_HI high byte уже содержит полученный байт.
Состояния, счётчики, reset, WREN/CS/SCK/MOSI, `ready/error/busy` и задержки
не изменяются. Bank input по-прежнему передаёт физический FRAM A16;
классификация полного PA22 выполняется upstream, до сужения адреса.

## Контракт результата

**`shared-rx` и `combined` намеренно меняют high `rdata` во время busy.**
Правильное полное значение гарантируется на `ready`, остаётся стабильным
в idle и совпадает с baseline также в DONE до ACK. Low byte совпадает
каждый такт. Для потребителя, требующего неизменного полного `rdata` во
время передачи, такая замена не подходит.

Проверен настоящий путь board → CPU. Board ACK использует `fram_ready`;
engine захватывает read data в MDR на завершении memory operation, а opcode
и decode ROM — на `fetch_capture`, квалифицированном request/ACK/no-error.
Промежуточное изменение `mem_read_data` не разрешает запись IR/MDR/ROM.
Исходники engine/decode/memory включены в verification manifest.
Это проверяется также полными CPU и cold-board прогонами ниже.

## Formal и unit tests

`tools/check_fram_cp47.py` выполняет шесть positive proofs: три варианта
при CLK_DIV=1 и 3. `byte-mux` сравнивается со всеми исходными outputs
каждый такт. Для shared receive применяется SAT temporal induction с
инвариантами, доказываемыми вместе с outputs:

- совпадают control state, counter/divider, TX, seen и все SPI/handshake pins;
- low `rdata` совпадает всегда, high — на ready, в IDLE/DONE;
- уже принятые младшие bits RX совпадают по маске числа sampled bits;
- state остаётся допустимым, active разрешён только в serial states.

Proof начинает с reset, затем все входы, включая последующие reset,
произвольны. Маска и debug outputs существуют только в formal harness,
в синтезируемый RTL не входят. Первоначальное сравнение без инварианта
частично принятого RX не смогло доказать shared-state преобразование;
усиление индукционного утверждения позволило доказать тот же контракт.
Это не ограничение MISO или последовательности команд assumption-ами.

Две намеренные ошибки — bank alias и неверный high byte byte-result —
отклоняются formal. Отдельный executable miter также обнаруживает обе
ошибки с настоящими counterexamples, а не только отказом proof.

`tools/check_fram_cp47_units.py` выполняет **18 прогонов**: для каждого
варианта и CLK_DIV=1/2/3 — четыре-состояния miter и независимый FRAM model
со scoreboard. На каждый вариант:

| CLK_DIV | Miter beats | Reset offsets | Сравнения тактов | Memory transactions |
|---:|---:|---:|---:|---:|
| 1 | 128 | 128 | 20561 | 2048 |
| 2 | 128 | 256 | 55441 | 2048 |
| 3 | 128 | 384 | 106705 | 2048 |

Итого по трём вариантам: **548121** сравнение тактов, **2304** reset offsets,
**18432** memory transactions. Reset sweep прерывает word/byte writes в
разных фазах; остальные beats включают reads/writes. Miter подаёт X/Z на
MISO и payload при известных control/address. Памятный scoreboard проверяет
оба банка, byte/word/odd, отсутствие повторного ACK при held request и
сравнивает все **128 КиБ** модели после операций. Strict Verilator lint
трёх generated RTL проходит без предупреждений.

## Полный CPU и board

Комбинированный вариант прошёл:

| Проверка | Результат |
|---|---|
| CPU portable: 4096 words × 8 upper pages × 18/22 | 67468380 clocks, 590096 PAR reads, 8 control beats |
| CPU vendor EBR: 4 words/page/mode | 97692 clocks, 848 PAR reads, 8 control beats |
| Portable/vendor edge cases | Wrap/NXM/I/O, MOVB lane/sign, odd vector4, high mapped stack/opcode/immediate — PASS |
| Full bus | 32 transaction checks, 6291456 PA/private-bypass combinations — PASS |
| Cold RT-11FB + DIR | 415159611 clocks, counts и UART совпали с CP45 |

CPU записал и прочитал каждое слово верхних 64 КиБ FRAM; snapshot нижнего
банка сохранился. Cold FB: 4311823 retirements, 5675704 reads / 489280 writes,
3980328 FRAM transactions, 300 RK commands, 576 timer edges, 3270 UART bytes,
162 SD reads / 6 RAM-overlay writes. Mapped/high-FRAM beats — 526239/65664,
MMR0 writes — 4. Clocks и UART byte-identical. Это simulation results,
аппаратные instructions/sec и новый Fmax не измерены.

MMU-enabled private ROM/DMA beats — 0. Active-MMU RK/high DMA и RT-11XM
не проверены. FIS corpus отдельно не повторялся: CPU/ALU/microcode прежние.
APR port RTL не менялся, его большой standalone corpus также не повторялся;
CSR и lookup задействованы CPU/board тестами. Vendor compile сохраняет
прежние предупреждения DP8KC/reference UART; portable builds чистые.

Оба backing disk image не изменены, включая
`../lsi11/disks/rt11v5.3/system.dsk`. MMU по-прежнему только kernel unified PAR:
нет PDR protection/W, MMR1/2, hardware fault metadata, abort250/restart,
modes/I-D/CSM/MAP и high DMA. FP11 отложен, FIS сохранён.

## Продолжение

После разрешения передачи выполнить полный HC1200 synthesis для baseline,
`byte-mux`, `shared-rx`, `combined` и сохранить raw reports/source snapshots.
Только измерения LUT/FF/EBR/Fmax определят, принимается ли какой-либо вариант.
Предельное попадание в 1280 LUT всё равно не заменяет резерв для оставшейся
MMU и обвязки. Не прошивать плату по одним simulation results.

```sh
python3 tools/build_fram_cp47.py
python3 tools/check_fram_cp47.py
python3 tools/check_fram_cp47_units.py
python3 tools/check_fram_cp47_negative.py
python3 tools/check_fram_cp47_system.py --variant combined --suite cpu
python3 tools/check_fram_cp47_system.py --variant combined --suite cpu --vendor --words 4
python3 tools/check_fram_cp47_system.py --variant combined --suite cpu --edges
python3 tools/check_fram_cp47_system.py --variant combined --suite cpu --vendor --edges
python3 tools/check_fram_cp47_system.py --variant combined --suite bus
python3 tools/check_fram_cp47_system.py --variant combined --suite board
```

Подготовленный `tools/checkpoint_fram_cp47.py` требует нового имени gate
и `--variant`; старые gates не перезаписывать. Build inputs CP44/CP45 и
обычные vendor models нужны как prerequisites. `tools/record_cp47.py`
фиксирует только текущее локальное состояние и намеренно требует обновления
после появления synthesis reports. [Manifest](verification-cp47.json),
[архив тестов](../tb/reports/cp47/).
