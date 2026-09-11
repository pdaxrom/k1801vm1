# CP56 — FRAM SPI за один такт процессора

На выбранной основе CP54b реализован экспериментальный контроллер с
**SCK 29,56 MHz вместо 14,78 MHz**, CPU остаётся на 29,56 MHz.
Локальные проверки прошли: простые R,R workloads ускорились в **1,70 раза**,
cold RT-11FB + DIR — в **1,665 раза**. Это измерения симуляции.
Полный **CP56a MAP/PAR/TRACE PASS: 1184 LUT / 339 FF / 6 EBR /
31,996 MHz**. На плате остаётся CP54b, default build — CP52a. Дополнительный
FRAM setup/hold TRACE прошёл с явно заданными PCB budgets; консервативный
запас длительности SCK всего **0,147 ns**, поэтому CP56 пока эксперимент.

## Основа по документации

[MR45V100A, FEDR45V100A-01](https://www.mouser.com/datasheet/2/348/FEDR45V100A-01-1280312.pdf),
страницы 7 и 15: READ `03` допускает SCK до **34 MHz**, остальные команды —
до 40 MHz. Для READ нужны high/low SCK не короче 13 ns; CS setup/hold и
время между транзакциями — 10 ns; MOSI setup/hold — 5 ns. Максимальная
задержка MISO после спада SCK — 12 ns при VCC >=2,7 V, 13 ns ниже.
Минимальная задержка изменения MISO — **0 ns**. Нельзя предполагать, что
старый бит держится после прихода следующего спада SCK на FRAM.

[MachXO2 High-Speed Interfaces, FPGA-TN-02153](https://www.latticesemi.com/view_document?document_id=39084),
§5.4.1 и §13.3: выходной ODDRXE позволяет передавать clock на pin;
SCLK должен использовать primary clock routing. Проверка штатной Diamond
модели подтвердила: D1 фиксируется на posedge SCLK и выходит на следующий
negedge. Поэтому разрешение последнего импульса нужно снимать заранее.

Уточнение после первой локальной проверки: ±5% из общего описания
[sysCLOCK](https://www.latticesemi.com/view_document?document_id=39080)
недостаточно для полного timing budget. В
[MachXO2 Family Data Sheet, FPGA-DS-02056-4.7](https://www.latticesemi.com/view_document?document_id=38834),
§3.24, таблица 3.30, приведены 125,685/133/140,315 MHz для commercial
OSCH, скважность **43–57%**, period jitter до **0,02 UIPP**; для более
низких частот относительный jitter меньше. Для проверки принят envelope
**+5,5%**, 43/57 и дополнительно полный 2% period jitter в худшую сторону.
При номинале 29,56 MHz это 31,1858 MHz, период 32,065876 ns. Минимальный
проверяемый период — 31,424559 ns; TRACE округлён консервативно до
**31,824 MHz**. Это принятый проверочный envelope, не измерение генератора.

## Изменение

`tools/build_spi_cp56.py` извлекает bus и FRAM из проверенного
`synth/reports/cp54b/source.tgz`, проверяя SHA256. Из CP55 shared RX ничего
не переносится. CPU, FIS, микрокод, прочая периферия и MMU-ветвь не меняются.
Новый профиль включается только явным `--spi-cp56`.

- SCK выводится через настоящий **ODDRXE**, D0=0, D1=разрешение импульса.
  В synthesis не входит portable модель `tb/models/ODDRXE.v`.
- На posedge CPU SCK переходит в 0, выставляется следующий MOSI;
  на negedge CPU SCK переходит в 1.
- MISO фиксируется на следующем posedge CPU, **до того как исходящий спад
  SCK дойдёт до FRAM и изменит её выход**. Для setup доступна почти целая
  длительность SCK с вычетом задержек. Три физических capture paths
  проверены отдельным TRACE ниже.
- На `bit_count=7` уже передаётся последний импульс; D1=0 запрещает следующий.
  Между байтами сохраняется один launch cycle: **9 вместо 17 CPU clocks**.
- RX остаётся отдельным, но хранит семь предыдущих битов; восьмой напрямую
  объединяется с MISO при записи результата. Нет делителя/фазового FF,
  PLL, аппаратного prefetch или новых CPU операций.
- `keep_read`, cursor, CS, byte/word, оба банка 128 КиБ, odd error,
  `ready/error/busy`, удержание запроса и прерывание reset сохраняют контракт.
  Поддерживается только `CLK_DIV=1`; иной параметр останавливает simulation.
- Reset маскирует D1 синхронно, как управление CS: текущий высокий
  полупериод не обрезается асинхронным reset примитива.

MAP/PAR подтвердил один ODDR на PB6C и PRIMARY `clk` от OSCH.
Число регулярных FF сократилось на два; ODDR указан отдельно в отчёте
I/O primitives. Всего 331 PFU FF и 8 PIO FF.

## Проверки

| Проверка | Результат |
|---|---:|
| Portable ODDRXE против неизменённого vendor ODDRXE | 3066 сравнений полупериодов/reset |
| Random FRAM, byte/word/odd, оба банка, полный 128 КиБ scoreboard | 4096 операций, portable + vendor |
| Sequential/redirect/write/bank/reset/hold/CS protocol | 654 операции |
| FRAM с задержками, X/Z MISO, крайними фазами reset | 7600 beats, 4480 reset offsets |
| Ошибки extra SCK pulse / stale last bit | Обе отвергнуты исполнимыми тестами |
| Board overlays/MAINT/byte lanes/KW11/UART/SD/RK/vector/DMA/RTI/panel | 43 beats |
| Полный компьютер, portable + vendor ROM и ODDRXE | 9 + 9 workloads, результаты совпали |
| Cold RT-11FB + DIR, FRAM и UART wire scoreboards | PASS |

В timing tests MISO становится X на спаде SCK и получает правильное
значение через 13 ns. Дополнительно моделируются выходные задержки
SCK/MOSI/CS **2–6 ns** и обратная линия **1–2 ns**, в четырёх комбинациях,
включая быстрый край OSCH +5%. Эти диапазоны **приняты для теста**;
измеренных задержек FPGA/PCB они не заменяют. Контролируются минимальные
SCK high/low, CS setup/hold/gap и MOSI setup/hold. Reset перебирается
с шагом четверть периода через READ и WREN/WRITE. Прерванная запись может
успеть записать байты; после reset сверяется фактическая память модели.

Standalone Verilator `--lint-only --Wall` нового RTL чистый. Штатный
ODDRXE из Diamond содержит четыре legal implicit wire `OP/ON/RSTB1/SR`:
Icarus `-Wall` их диагностирует. Модель сохранена без правок; runner
разрешает ровно эти четыре строки из этого файла, любая новая диагностика
проваливает проверку. Диагностики и hashes сохранены, глобального подавления нет.
CPU/FIS corpus заново не запускался: core/datapath/microcode не менялись.

## Измерения

Каждый warm workload — 256 PDP-11 instructions, включая одинаковые
переходы через каждые 64 инструкции. Число memory beats, opcode fetches,
writes, SCK и транзакций CS совпало с CP54b. Уменьшились CPU clocks,
длительность request и FRAM busy. Это полный board с FIS и периферией,
но warm fixture подавляет boot overlay и предварительно заполняет FRAM.

| Workload | CP54b clocks | CP56 clocks | CP56 CPI | Ускорение |
|---|---:|---:|---:|---:|
| MOV / ADD / CMP / mixed R,R, каждый | 10256 | 6032 | 23,5625 | 1,700× |
| MOV memory → register | 56876 | 32492 | 126,9219 | 1,750× |
| MOV register → memory | 61916 | 35516 | 138,7344 | 1,743× |
| MOV memory → memory | 88628 | 50132 | 195,8281 | 1,768× |
| BR self | 27392 | 15104 | 59 | 1,814× |
| Stack push/pop | 59812 | 34404 | 134,3906 | 1,739× |

При номинальных 29,56 MHz R,R workload даёт **1,2545 млн инструкций/с**
против 0,7378 млн у CP54b. Это пересчёт clocks, не замер реальной платы.

Cold boot идёт из reset через SPI bootstrap, без подмены CPU state:
**173379163 clocks**, прежде 288686609. **300 RK commands, 278 timer edges,
3270 UART bytes, 162 SD reads/6 writes**. Retired 3971490, reads 5192659,
writes 419984, FRAM transactions 2409454. Меньшее число timer interrupts
меняет retired/memory counters; полного потактового равенства не ожидается.

Каждый UART pin byte совпал с принятыми CSR writes, включая stop bits.
Каталог от `SWAP.SYS` до последнего prompt совпал с CP54b байт-в-байт.
Во время ввода DIR иначе перемежаются echo и перерисовка prompt после
`SET SL ON`; raw UART целиком не совпадает. Это известная асинхронная
последовательность guest console, итог DIR одинаков. Все **26 bus faults**
опроса отсутствующего оборудования совпали с прежним запуском.

Backing image `lsi11-fpga/images/rt11v503.dsk` не менялся; SD-записи идут
в RAM overlay модели. SHA256:
`e769228f2e1262220297bfa98b8f2841688849ab4c49ad9cd48d0d73d0a99553`.
Это RT-11FB без MMU; новых утверждений об RT-11XM нет.

## Синтез и mapping

После явного согласия пользователя согласованные **7 файлов / 51074 байт**
переданы на `sash@192.168.1.108:/tmp/uj11-cp56-20260911`. Архив 14584 байта,
SHA256 `dbe8582f6fca92fe43e9ed4c7532d9cc254b45314de3ad9293ddc2df78d70bd6`.
Остальные 37 inputs скопированы из CP55 на сервере после проверки hashes.
Дисковые образы и `microasm11` не передавались. Исходники, локальная
simulation и manifest synthesis совпали; после анализа hashes сохранены.

Один полный gate: **LCMXO2-1200HC-4SG32C**, Diamond 3.14.0.75.2 /
Synplify V-2023.09L-2; CPU/FIS/FRAM/KL11/KW11/panel/HG/SD/RK/firmware/
OSCH/reset/pins, MMU отключён, 954/1024 microinstructions.

| Gate | LUT4 | FF | EBR | Slices | Fmax MHz | Nominal slack ns |
|---|---:|---:|---:|---:|---:|---:|
| CP54b, установлен | 1185 | 341 | 6 | 595 | 32,273 | 2,843 |
| CP56a, эксперимент | 1184 | 339 | 6 | 595 | 31,996 | 2,575 |

Свободны **96 LUT, 45 slices, 1 EBR**; до цели <=1100 нужно убрать
84 LUT. PIO свободных нет. MAP LUT: **1056 logic + 48 RAM + 80 carry**.
EDIF: 2998 nets без нескольких сильных драйверов; пять прежних FRAM
CCU2D и один настоящий ODDRXE. Несколько двунаправленных сетей описаны
отдельно; этот аудит не является проверкой физического contention.

Удалены обычный `spi_sck` FF и `rx[7]`; `rx[0]` переехал из PIO в PFU,
`rdata[0]` — из PFU в PIO. Итог — **−2 регулярных FF и +1 ODDR primitive**.
MAP сообщает 8 PIO FF, ODDR usage отдельной строкой; это не подсчёт всех
внутренних триггеров примитива как RTL bits.

Критический путь: EBR lane 3 → write/memory qualification → DMA operand →
I/O page/ACK → microsequencer condition/next address → EBR lane 2.
**31,280 ns, 16 уровней, 59,6% routing**, slack 2,575 ns при 29,56 MHz.
Новых pipeline stages или изменений CPU ISA нет; худший путь по-прежнему
проходит через общую память/ACK. Один PAR run не отделяет влияние logic
mapping от placement/routing.

MAP: три прежних предупреждения, ноль ошибок. У Synplify добавился один
**MT246**: отсутствует пользовательская timing model blackbox ODDRXE.
Это ограничение оценки Synplify; Diamond MAP распознал штатный primitive,
а физический TRACE использовал реальный IOL_B6C и связанные timing arcs.
Остальные коды/counts warnings совпали с CP54b (BN161 по-прежнему ограничен
100 выведенными сообщениями). Нового runtime RTL blackbox в MAP нет.

## Дополнительный внешний TRACE и оставшийся предел

Исходный comparable gate сохраняет `external_pin_delays_constrained=False`.
Отдельные TRACE/SDF exports используют **ту же неизменённую routed NCD**;
повторного synthesis/PAR нет. В budget TRACE задано:

- CPU period с запасом на OSCH +5,5% и 2% jitter: **31,824 MHz**.
- SCK clock-to-pad: **2,2–8 ns**; измеренные fast/slow offsets 2,219/7,798 ns.
- MISO input delay **23 ns** = 8 ns SCK output + 2 ns PCB round trip +
  13 ns FRAM tCLQV. Минимальное новое MISO — 2,2 ns после oscillator edge
  при PCB >=0 и tCLQX=0; в синтаксисе Diamond это **HOLD -2.2 ns**.
- MOSI/CS сравниваются с реальным SCK pin через `CLKOUT PORT`.
  Для предположения PCB skew <=2 ns и короткого полупериода >=13,14 ns
  получены окна **±6,14 ns MOSI** и **±1,14 ns CS** относительно спада SCK.
  Проверка CS hold консервативна: normal DONE размыкает CS позднее.

Первичные probe runs с нулевым incoming hold и затем ошибочным положительным
знаком HOLD сохранены; они ожидаемо показали три hold violations. Итоговый
budget соответствует **данные меняются после**, а не до reference edge.
В финальном TRACE **0 setup / 0 hold errors**; покрыты все три MISO endpoints,
включая PIO `rdata[0]`, PFU `rx[0]` и PFU `rdata[8]`.

| Проверка | Setup slack ns | Hold slack ns |
|---|---:|---:|
| Internal CPU, 31,824 MHz | 0,169 | 0,291 |
| MISO | 7,512 | 1,080 |
| SCK absolute envelope | 0,202 | 0,019 |
| MOSI относительно SCK | 6,817 | 6,102 |
| CS относительно SCK | 0,604 | 1,374 |

SCK hold slack 0,019 ns относится к выбранной границе clock-to-pad 2,2 ns,
а не к hold margin MISO. Оставшиеся unconstrained connections относятся
к прежним UART/SD/keyboard/reset; этот анализ покрывает FRAM, не все
внешние устройства компьютера.

Fast/slow SDF имеют одинаковые modeled rise/fall delays у ODDRXE,
output buffer и соответствующих interconnect. **SDF-annotated simulation
не выполнялась**. Отдельно выполнены ещё четыре protocol simulations с
routed output offsets и high/low **13,147009 / 18,277549 ns** в обоих порядках:
3800 beats и 2240 reset offsets, PASS. Реальные input paths проверяет TRACE,
а эти tests — фазу и поведение контроллера; это не полная gate simulation.

Главный предел: `T × (0,43 − 0,02) = 13,147009 ns`, при требуемых FRAM
13 ns запас только **0,147009 ns**. Добавление в исполнимый negative test
0,2 ns задержки фронта закономерно нарушает SCK high timing. Модельные
симметричные delays не доказывают столь малую асимметрию настоящих pin/PCB.
Проверка setup/hold прошла при заданных PCB budgets, но **physical pulse-width
signoff не завершён**. Ни эти budgets, ни форма SCK осциллографом не измерялись.

Поэтому установленная CP54b и default CP52a сохранены. CP56a — полезный
эксперимент с подтверждённым выигрышем и fit. Перед установкой нужно
увеличить запас SCK (например, исследовать clock с контролируемой скважностью)
либо проверить/ограничить реальные условия на плате. Просто считать
29,56 <34 MHz достаточным доказательством нельзя.

Архивы: `synth/reports/cp56a` (raw reports, source, EDIF, `io-timing`),
`tb/reports/cp56-clock` (добавочные тесты); сводный проверяемый manifest —
[synthesis-cp56.json](synthesis-cp56.json). [verification-cp56.json](verification-cp56.json)
сохранён как исторический snapshot локальной фазы до разрешения synthesis.

## Воспроизведение

```sh
python3 tools/build_spi_cp56.py
python3 tools/run_spi_cp56.py
python3 tools/run_board.py --tag cp56-final --spi-cp56
python3 tools/checkpoint_board.py cp56a --spi-cp56 --prepare-only
python3 tools/record_spi_cp56.py
```

Vendor models берутся из установленного Diamond MachXO2 simulation library
в `build/vendor`: ODDRXE, GSR, PUR, DP8KC. Они не синтезируются.
Для реального gate на сервере последняя checkpoint-команда выполняется
без `--prepare-only` в новом каталоге. Все проверенные файлы, logs и source
snapshot: [verification-cp56.json](verification-cp56.json), `tb/reports/cp56`.

Выполненная серверная команда: `python3 tools/checkpoint_board.py cp56a --spi-cp56`,
затем `python3 tools/archive_synthesis.py cp56a`. Дополнительные TRACE/ldbanno
команды и точные PRF сохранены в `synth/reports/cp56a/io-timing/*.sh`.
Они читают routed NCD CP56a и не изменяют её. NCD/EDIF/PRF hashes —
`io-timing/routed-inputs.json`.

После получения этих артефактов локально:

```sh
python3 tools/check_timing_cp56.py
python3 tools/record_synthesis_cp56.py
```

Для повторного анализа compressed `.twr/.sdf/.vo.gz` раскрываются в
`build/cp56-timing`, остальные файлы `io-timing` копируются туда без изменения.
EDIF раскрывается в `build/cp56-netlist/cp56a_impl1.edi`. Повторная физическая
разводка требует нового checkpoint, её цифры нельзя подменять в CP56a.
