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

Оба преобразования комбинационные: **ноль новых FF**, никакой регистрации
ACK, паузы или дополнительного такта памяти. Это число добавленных RTL
state bits; реальное число FF после synthesis пока не измерено.

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

**Новых LUT/FF/EBR/Fmax пока нет.** Контроль CP53a — 1195 LUT / 341 FF /
6 EBR / 602 slices / 31,338 MHz. Оба gate подготовлены для полного
HC1200 computer, LCMXO2-1200HC-4SG32C, 29,56 MHz, microstore 954 слова.

Автоматическая проверка отклонила передачу CP54: прежнее согласие сочтено
ограниченным пакетом CP53. Запрошено подтверждение **9 файлов, 71944 байта**
(RTL, scripts, manifests; `/tmp/cp54-files.txt`) на
`sash@192.168.1.108:/tmp/uj11-cp54-20260911`, затем два synthesis.
Передача не состоялась. Нужные неизменённые CP53 inputs уже есть на сервере;
перед копированием и после synthesis проверяются все source hashes.
Дисковые образы и `microasm11` в пакет не входят.

До реальных ресурсов победитель не выбирается и default не меняется.
Внутренний TRACE Fmax не заменяет проверку внешних pin delays и платы.

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
python3 tools/checkpoint_board.py cp54a --ack-cp54 dma --prepare-only
python3 tools/checkpoint_board.py cp54b --ack-cp54 dma-ack --prepare-only
```

Для synthesis требуется Diamond на сервере; `--prepare-only` убирается
после разрешённой передачи. Архив исходников/результатов локальных tests:
`tb/reports/cp54`. Профиль `--ack-cp54` взаимоисключающий с MMU и другими
experimental board flags.
