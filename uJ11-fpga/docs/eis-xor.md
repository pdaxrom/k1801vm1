# CP24: XOR через существующие ALU и destination EA

**CP24 завершён: синтез и полная portable/vendor регрессия прошли.**
CP24 добавляет только XOR. Адреса 16-битные, MMU отсутствует. RF 16×16,
ALU/Q, PSW, microsequencer, memory/FRAM и instruction-stream prefetch сохранены.
Единственный изменённый RTL-модуль — decoder. C executor сохранён побайтно.

## Семантика и источники

[DEC J-11 User Guide, Oct 1983](../../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf),
printed **6-44 / PDF page 143**, определяет opcode `074RDD`, word XOR и запись
результата в destination. N/Z соответствуют результату, V очищается, C сохраняется.
Страница прочитана визуально. Byte-варианта у XOR нет.

Таблица **C-1, printed C-6 / PDF page 229** того же руководства указывает для
J-11 чтение register source после auto-increment/decrement destination register;
для PC source с extension word используется PC после чтения extension.
Строки и колонка J-11 проверены визуально.
[SIMH XOR executor](https://github.com/simh/simh/blob/master/PDP11/pdp11_cpu.c)
также выполняет destination ReadMW/GeteaW перед чтением register source для
соответствующего класса CPU. Это дополнительная сверка, а не подмена руководства.

В [нашем C executor](../../core/core.c), case 0074, DCJ11 уже читает register
после DECODE_DST/GET_WORD. PUT_WORD проверяет abort до изменения flags. Здесь
исправления C не потребовались; instrumentation остаётся наблюдательной.

[AM4 microcode](../../lsi11-fpga/ucode/experimental/am4/mc.asm), entries
0d0–0d4: OR_RR выбирает путь, CALL с OR_MD получает destination, затем XOR
читает As0. uJ11 использует тот же принцип позднего source, но прежний общий EA
и прямой decoder bypass для register mode.

Пример: `XOR R0,(R0)+`, R0=4000 hex, operand=a55a hex. После EA R0=4002;
в 4000 записывается e558, C сохраняется. Раннее сохранение R0 дало бы e55a —
это отдельный отрицательный контроль. При failed READ/WRITE flags не меняются;
успевшие EA side effects и frame vector 004 проверяются существующим oracle.

## Пять новых микрокоманд

597/1024×36, encoding v11. Все **592 прежних слова и 242 именованные метки**
CP23 сохранены. Добавлены пять слов и три метки; формат assembler не менялся.

| Entry | Микрокод | Назначение |
|---|---|---|
| 02c | XOR_RR | RS XOR RD → RD, NZV, FETCH; один execution cycle |
| 02e–02f | XOR_EA | CALL DESTINATION_EA, READ по T1 |
| 094–095 | XOR_MEMORY | RS XOR MDR → T2; WRITE T2 по T1 |
| 3b4, прежний | BIS_MEMORY_FLAGS | NZV от T2 после успешной записи, FETCH |

Декодер добавляет сравнение `IR[15:9] == 7'o74`. Destination mode выбирает
02c либо 02e; source mode dispatch для XOR не нужен, источник всегда register.
Все восемь addressing modes используют прежние правила SP/PC, signed
displacements и odd-word faults. Immediate destination остаётся настоящим
read/modify/write слова instruction stream. Не добавлены ALU mux, RF ports,
регистры или аппаратный счётчик.

## Проверки

RAM/FRAM × portable/vendor проверки прошли: **11028 normal cases / 11224 candidates**,
196 явных exclusions (abort 128, vector 128, I/O 68, причины пересекаются;
stack 0, odd 0). Normal corpus покрывает 504 encodings; восемь `@-(PC)` encodings
покрыты отдельной fault suite. Вместе normal/fault покрывают все 512.

**5760 fault frames:** 4768 ACK errors и 992 odd-word, exclusions 0. Проверяются
все Rs, destination modes/registers, каждый operand/pointer/extension/write
bus beat, четыре исходных PSW и задержки ACK. После captured frame C не
изменяет регистры/PSW и не добавляет bus accesses.

Дополнительная независимая Python-модель проверяет **4698 обычных C records**:
все восемь word addressing modes, позднее чтение Rs, результат XOR, flags,
все registers и точную последовательность bus accesses; **385 alias cases**.
В этот дополнительный model check не входят T/IRQ frames: их сверяют RTL и
существующий C executor. Expected results никогда не переписываются.

Directed test с read-clear CSR: **1024 cases на ROM-модель**, все NZVC и 0..3
waits. Проверяются ровно одно чтение и одна запись, исходное прочитанное
значение, failed READ/WRITE, odd address и стабильность запроса до ACK. Это
модель CSR на core interface; полный board bus сюда не включён.

Восемь negative controls обнаруживают: отсутствующий opcode, OR вместо XOR,
неверный source selector, потерю C, установку V, ранний source snapshot,
flags до успешного WRITE и byte WRITE вместо word. Они собираются из настоящих
архивов CP23c/CP24a и должны дать architectural/bus mismatch, а не ошибку сборки.

Полная свежая регрессия прошла: **242133 instruction cases и 41596 fault frames**
на каждом RAM/FRAM × portable/vendor сочетании, **1018 benchmarks на ROM-модель**.
Совпали все **112 portable/vendor result files**. Все **20 прежних C fixture
files, 40 cycle CSV и 64 benchmark JSON** побайтно равны CP23, включая такты,
memory beats и SPI activity. Из illegal candidate list удалены 512 XOR
encodings; 4160 completed illegal fixtures остались прежними, поскольку
C-эталон уже умел XOR и исключал их. Осталось 15136 illegal candidates,
из них 10976 exclusions; эти исключения явно сохранены в oracle log.

Также прошли прежние unit/directed tests, lint, проверки периферии lsi11,
общая C core regression и decoder miter всех 65536 encodings. Изменились
ровно 512 XOR encodings. Микросеквенсор побайтно равен CP23; его прежнее
formal proof сохранено, новая formal verification в CP24 не заявляется.
Проверены 102 synthesis source archives и 403 raw report hashes; текущие
fit inputs совпадают с CP24a/b. [Manifest](verification-cp24.json),
[benchmark и cycle totals](benchmarks-cp24.json).

```
make test-eis-xor test-eis-xor-fault test-xor-io
make test-xor-oracle test-xor-negative
make benchmark-eis-xor
make verify-cp24
```

## Измеренный HC1200 fit

Diamond 3.14.0.75.2/Synplify, LCMXO2-1200HC-4SG32C, свежие MAP/PAR/TRACE:

| Archive | Scope | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---|---:|---:|---:|---|---:|
| [CP24a](../synth/reports/cp24a/result.json) | core+probe | 854 | 299 | 4 | 35 MHz PASS | 36.059 MHz |
| [CP24b](../synth/reports/cp24b/result.json) | FRAM/prefetch/IRQ+probe | 1089 | 416 | 4 | 29.56 MHz PASS | 30.273 MHz |

К CP23 добавлены 4/2 LUT, FF/EBR прежние. MAP FRAM:
975 logic + 48 distributed RAM + 66 ripple = 1089 LUT, 546 slices.
До желательных 1100 остаётся **11 LUT**, до
физических 1280 — 191. Полные UART/timer/panel/SD/RK и внешние pin delays в fit
не входят. 50 MHz и предпочтительные 900–1000 LUT не достигнуты.

CP24b critical path: 33.059 ns, 20 logic levels, 57.2% routing, setup margin 0.796 ns.
EBR lane 2 → register/address selection → FRAM match/buffer/ACK → sequencer
condition/next_address → EBR lane 1 address. Fmax 30.273 вместо 30.193 MHz CP23;
различие placement не позволяет приписать весь delta операции XOR.
FPGA не программировалась; MUL/DIV потребуют отдельных HC1200 checkpoints.

## Производительность

Новые benchmarks: 8 destination modes × 4 memory modes. В теле 31 XOR и BR;
для modes 2–5 добавлен MOV #4000,R2 для восстановления указателя. Этот MOV
учтён в retirements. Один warmup loop исключается, затем измеряются 8 loops:
256 или 264 instructions. Проверяются все registers, flags и слова результата.

На ideal RAM register loop: **512 clocks / 256 retirements = 2 CPI**,
256 demand beats. Измеряется throughput смеси 31 XOR + BR.
При CPU/SPI **29.56/14.78 MHz** register loop на FRAM с prefetch:
**10280 clocks / 256 retirements = 40.156250 CPI**,
расчётно **736,125 instructions/sec**. Это функциональная
симуляция реального SPI transport, не измерение физической платы.

| Destination mode | Ideal RAM CPI | Legacy FRAM CPI | Sequential FRAM CPI | FRAM + prefetch CPI | Demand beats/insn, prefetch | Расчётные insn/sec @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 2.000000 | 107.000000 | 41.125000 | 40.156250 | 1.000000 | 736,125 |
| 1 | 9.750000 | 334.656250 | 334.656250 | 334.656250 | 2.937500 | 88,329 |
| 2 | 10.909091 | 332.333333 | 328.212121 | 328.030303 | 2.909091 | 90,114 |
| 3 | 12.787879 | 432.848485 | 428.727273 | 428.545455 | 3.848485 | 68,978 |
| 4 | 10.909091 | 332.333333 | 328.212121 | 328.030303 | 2.909091 | 90,114 |
| 5 | 11.848485 | 431.909091 | 427.787879 | 427.606061 | 3.848485 | 69,129 |
| 6 | 11.687500 | 438.312500 | 372.437500 | 372.437500 | 3.906250 | 79,369 |
| 7 | 13.625000 | 541.968750 | 476.093750 | 476.093750 | 4.875000 | 62,089 |

Полные счётчики FRAM + prefetch за восемь измеряемых loops:

| Destination mode | Retirements | Microclocks | Demand beats | SPI transactions | SPI clocks |
|---|---:|---:|---:|---:|---:|
| 0 | 256 | 10280 | 256 | 8 | 4352 |
| 1 | 256 | 85672 | 752 | 1000 | 38080 |
| 2 | 264 | 86600 | 768 | 1000 | 38336 |
| 3 | 264 | 113136 | 1016 | 1248 | 50240 |
| 4 | 264 | 86600 | 768 | 1000 | 38336 |
| 5 | 264 | 112888 | 1016 | 1248 | 50240 |
| 6 | 256 | 95344 | 1000 | 1000 | 42048 |
| 7 | 256 | 121880 | 1248 | 1248 | 53952 |

Все 32 новых и 986 прежних benchmarks прошли с portable и vendor EBR.
Старые значения тактов, memory beats и SPI activity не изменились.
[Исходные JSON и производные CPI/IPS](benchmarks-cp24.json).

В старом описании CP22 исправлена ошибка единиц: сохранялись 235 именованных
меток CP21; 551 было количеством микрокоманд. Исходники и raw reports прежних
измерений сохранены. Это уточнение документации, не изменение RTL.
