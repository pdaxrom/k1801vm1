# CP54 — декодирование I/O и быстрый ACK без дополнительных тактов

Основа — точные `uj11_board_bus.v` и `uj11_board_fram.v` из проверенного
source archive **CP53a**. CPU/FIS, ROM/microcode, FRAM transport, периферия
и сохранённая MMU-ветвь не меняются. Варианты генерируются отдельно в
`build/cp54-ack`; основной RTL и плата остаются прежними.

## Причина эксперимента

В TRACE CP53a худший путь проходит через EBR → dynamic RF/address →
I/O/DMA decode → ACK → bus-fault predicate/microsequencer → EBR:
**31,936 ns**, 17 уровней, 58,9% routing, slack 1,919 ns при 29,56 MHz.
В нём видны `io_page_1`, `service_dma_selected_0` и последующие ACK nets.
Это основание исследовать данный участок, но не обещание улучшения mapping.

Исходный DMA select включает признак I/O page, после чего тот же признак
повторно используется в разрешении CPU-visible I/O:

```verilog
service_dma_selected = io_page && dma_operand;
cpu_io_page = io_page && !service_dma_selected;
```

Для двоичных входов это точно равно
`cpu_io_page = io_page && !dma_operand`. `dma_operand` зависит только от
RK service state, write/read и instruction-fetch; адрес в него не входит.
Отдельный `service_dma_selected` сохраняет полную квалификацию по адресу.

## Два варианта

| Gate | Вариант | Изменение |
|---|---|---|
| CP54a | dma | Вынесен независимый от адреса service_dma_operand; I/O квалифицируется им напрямую |
| CP54b | dma-ack | То же, плюс общий последний gate для быстрых ACK MAINT/KW11/panel/fixed RK |

В B до общего `request && cpu_io_page` вычисляются только младшие адресные
сравнения и условия fixed RK response. ACK от UART, SD, FRAM и обоих ROM
сохраняются как прежде, включая их error gating. Неподдерживаемый I/O
по-прежнему не отвечает, physical RK DMA имеет приоритет над CSR.

Оба преобразования комбинационные: никакой регистрации ACK, паузы или
дополнительного такта памяти. Реальный synthesis подтвердил прежнее число
FF — **341**, включая 333 PFU и 8 PIO registers.

## Проверки

`build_ack_cp54.py` проверяет побайтовую неизменность state-update blocks,
peripheral instances и read-data mux. `check_ack_cp54.py` извлекает реальные
комбинационные участки RTL и сравнивает **39 output bits**: все 22 исходных
selectors, 16-bit rdata и ACK. В SAT не ограничены адреса, направления,
состояния overlay/RK, device ready/error/ACK, данные и enable parameters.
Приватные wires переименованы, чтобы их совпадающие имена не служили
недоказанными промежуточными предположениями.

- Два положительных SAT proofs PASS. Три negative controls с разрешённым
  I/O при DMA или ACK вне I/O отвергнуты.
- 524288 four-state cases: X/Z в выбранных и невыбранных словах данных
  устройств, все 65536 адресов и псевдослучайные состояния. Адрес/control
  здесь известны; неизвестные управляющие входы не объявляются эквивалентными.
- 86 полных board beats: overlay release/reset, byte lanes, KW11, UART/SD
  side effects, RK CSR/interrupt/vector/private DMA/RTI, panel/host pins.
- По девять portable и Lattice EBR workloads для каждого варианта,
  **36 измерений**. Все raw counters точно совпали с CP53a: clocks, bus
  beats, opcode fetches, writes, request/busy, FRAM CS и SCK.
  MOV/ADD/CMP R,R — **40,0625 CPI**, memory/stack/BR workloads прежние.
- Verilator сначала обнаружил несовпадение ширин 12/13 bits в новом
  сравнении MAINT. Выражение исправлено на сравнение одинаковых 13-bit
  величин; финальные formal, Icarus и Verilator gates прошли без suppression.

Оба новых холодных RT-11FB + DIR **PASS**: каждый по **288686609 clocks**,
300 RK commands, 467 timer edges, 3270 UART wire bytes, 162 SD reads/6 writes.
Retired 3979364, reads 5207588, writes 422214, FRAM transactions 2422032.
Все counters и raw UART bytes точно совпали с CP53a. Каждый принятый
FRAM beat проверен по модели, UART/SD writeback/IRQ assertions прошли.
Сборки Verilator и Icarus завершились без предупреждений и ошибок.

Backing image `lsi11-fpga/images/rt11v503.dsk` не изменён,
SHA256 `e769228f2e1262220297bfa98b8f2841688849ab4c49ad9cd48d0d73d0a99553`.
Это MMU-less FB, новый RT-11XM test не заявляется.
Результаты, hashes и logs — [verification-cp54.json](verification-cp54.json).

## Synthesis

После явного разрешения пользователя пакет **9 файлов, 71944 байта**
передан на `sash@192.168.1.108:/tmp/uj11-cp54-20260911`. Неизменённые
исходники скопированы из CP53 на сервере после проверки SHA256; все manifests
до и после сборок точно совпали с согласованными и протестированными RTL.

Оба полных HC1200 gates **MAP/PAR/TRACE PASS**, полностью разведены.
Diamond 3.14.0.75.2 / Synplify V-2023.09L-2, LCMXO2-1200HC-4SG32C,
constraint 29,56 MHz, полный CPU/FIS/FRAM/KL11/KW11/panel/HG/SD/RK,
firmware/OSCH/reset/pins. Microstore — **954/1024 слова**.

| Gate | LUT4 | FF | EBR | Slices | Fmax, MHz | Slack, ns |
|---|---:|---:|---:|---:|---:|---:|
| CP52a, default | 1159 | 326 | 6 | 584 | 31,470 | 2,053 |
| CP53a, прежний sequential candidate | 1195 | 341 | 6 | 602 | 31,338 | 1,919 |
| CP54a, dma | 1192 | 341 | 6 | 600 | 31,524 | 2,107 |
| **CP54b, dma-ack** | **1185** | **341** | **6** | **595** | **32,273** | **2,843** |

**Выбран CP54b** для дальнейшей оптимизации sequential FRAM board:
относительно CP53a **−10 LUT / −7 slices / +0,935 MHz**, FF/EBR и все
execution counters прежние. CP54a тоже улучшил mapping, но уступает B
по площади и timing. От первого sequential CP52b суммарно сэкономлено
13 LUT; ускорение R,R 2,671× и cold FB+DIR 1,229× против default сохранено.

Остаются **95 LUT / 45 slices / 1 EBR**, свободных PIO sites нет.
Цена ускорения против default — **+26 LUT / +15 FF**. Цель <=1100 LUT
пока не достигнута; default остаётся CP52a, кандидат B сохранён отдельно.
Физическая плата остаётся CP29a; новая прошивка в этом checkpoint не делалась.

### Mapping и критический путь

MAP раскладывает CP53a как 1067 logic + 48 distributed RAM + 80 carry LUT;
CP54a — 1064/48/80, CP54b — **1057/48/80**. Уменьшилась логическая часть,
состав памяти и carry не изменился. В конечном FRAM netlist обоих вариантов
по-прежнему пять CCU2D для сравнения cursor.

Иерархические Synplify counts нельзя выдавать за независимую цену модулей:
при одинаковом FRAM RTL его ORCALUT4 counts составляют 107 у CP53a,
132 у CP54a и 109 у CP54b; глобальная оптимизация перераспределяет логику.
Решение принято по конечному MAP всего компьютера, а не сумме таких counts.

В CP54b худший путь теперь проходит через **address[0] → qualification
request/write → service_dma_operand → ACK → bus-fault/fault_redirect →
microsequencer**, от EBR lane 2 к EBR lane 3. **31,012 ns, 17 уровней,
58,4% routing**, slack 2,843 ns. Прежний I/O-prefix путь больше не худший,
но зависимость от младшего адресного бита и проверки нечётного слова остаётся.
Число logic levels самого худшего пути не сократилось; это другой путь.
В CP54a: 31,748 ns, 16 уровней, 61,3% routing, destination decode-ROM.

Проверены **2983/3059 сети EDIF**, сильных multiple drivers нет; INOUT nets
фиксируются отдельно, это не проверка физического contention на выводах.
Набор кодов и число выведенных Synplify warnings совпали с CP53a, включая
100 BN161. MAP сохранил три прежних предупреждения — JTAG/GPIO,
configuration ports и local timer reset — при нуле ошибок. Raw warnings
сохранены. OSCH/inferred-clock сообщения Synplify не подменяют итоговый
TRACE, который использует явный LPF 29,56 MHz. Внешние pin delays всё ещё
не заданы; Fmax относится к внутреннему timing, не к измерению платы.

[Проверенные измерения и hashes](synthesis-cp54.json), source snapshots,
MAP/PAR/TRACE и сжатые EDIF — `synth/reports/cp54a/b`. Локальные tests
завершены до synthesis; повторные cold/vendor прогоны не запускались,
поскольку доказано точное совпадение всех соответствующих HDL inputs.

## Воспроизведение

```sh
python3 tools/build_ack_cp54.py
python3 tools/check_ack_cp54.py
python3 tools/run_ack_cp54.py
python3 tools/run_ack_cp54.py --vendor dma
python3 tools/run_ack_cp54.py --vendor dma-ack
python3 tools/run_board.py --tag cp54-dma --ack-cp54 dma
python3 tools/run_board.py --tag cp54-dma-ack --ack-cp54 dma-ack
python3 tools/record_ack_cp54.py
python3 tools/checkpoint_board.py cp54a --ack-cp54 dma
python3 tools/checkpoint_board.py cp54b --ack-cp54 dma-ack
python3 tools/archive_synthesis.py cp54a cp54b
python3 tools/record_synthesis_cp54.py
```

Выше перечислены выполненные команды; повторный synthesis требует свежих
имён gates, исходники и EDIF для аудита восстанавливаются из архивов.
`--prepare-only` создаёт проект без запуска Diamond. Архив локальных tests:
`tb/reports/cp54`. Профиль `--ack-cp54` взаимоисключающий с MMU и другими
experimental board flags.
