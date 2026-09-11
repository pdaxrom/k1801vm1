# CP56 — FRAM SPI за один такт процессора

На выбранной основе CP54b реализован экспериментальный контроллер с
**SCK 29,56 MHz вместо 14,78 MHz**, CPU остаётся на 29,56 MHz.
Локальные проверки прошли: простые R,R workloads ускорились в **1,70 раза**,
cold RT-11FB + DIR — в **1,665 раза**. Это измерения симуляции.
**CP56 ещё не синтезирован и не прошит.** На плате остаётся CP54b,
default build остаётся CP52a. Новые LUT/FF/EBR/Fmax пока неизвестны.

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

[MachXO2 sysCLOCK](https://www.latticesemi.com/view_document?document_id=39080):
номинал OSCH 29,56 MHz имеет допуск ±5%. Верхний край — 31,038 MHz,
ниже предела READ. Это проверка ограничения FRAM по частоте, а не
доказательство закрытия FPGA и внешнего timing.

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
  длительность SCK с вычетом задержек. Фактический hold ещё нужно проверить.
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

Реальное отображение ODDRXE на PB6C, первичную clock-сеть и расходы
I/O-регистров нужно подтвердить MAP/PAR. Изменение ширины RTL-регистров
не выдаётся за измеренную экономию FF.

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

## Оставшийся gate

Подготовлен CP56a для **LCMXO2-1200HC-4SG32C**, full board, clock 29,56 MHz.
Автоматический approval review отклонил экспорт исходников CP56, потому
что прежнее разрешение было сочтено относящимся только к прежним пакетам.
Новый payload пока не передан, synthesis не выполнялся.

Подготовлены **7 файлов / 51074 байт**, архив **14584 байта**;
назначение `sash@192.168.1.108:/tmp/uj11-cp56-20260911`.
Список `/tmp/cp56-files.txt`, hashes — в архивном
`tb/reports/cp56/cp56-transfer-manifest.json`. Только RTL, генератор и
manifests uJ11; остальные inputs берутся из ранее проверенного CP55
на сервере с проверкой hashes. Дисковые образы и `microasm11` не входят.
Архив SHA256:
`dbe8582f6fca92fe43e9ed4c7532d9cc254b45314de3ad9293ddc2df78d70bd6`.

После разрешения нужно выполнить MAP/PAR/TRACE, сравнить ресурсы и
критический путь с CP54b **1185 LUT / 341 FF / 6 EBR / 32,273 MHz**.
Затем отдельно проверить внешние input/output setup/hold FRAM с реальными
routed pin delays, включая задержку возврата MISO, минимальную tCLQX=0,
скос CS/MOSI/SCK и быстрый край OSCH. Подготовленный frequency-only gate
этого не доказывает; `external_pin_delays_constrained=False` указан явно.
До этих проверок CP56 не принимается для установки на плату.

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
