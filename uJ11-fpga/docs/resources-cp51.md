# CP51 — ресурсы и узкие места MMU-less uJ11

Дата: 2026-09-11. Основа оптимизации — полный HC1200 board без MMU, с FIS,
SPI FRAM, SD/RK, UART, KW11, HDSP/RGB/keyboard/HG. MMU остаётся отложенным.

## Что измерено и к чему относятся цифры

Последний полный MAP/PAR/TRACE этой логики — **CP40h**, Diamond 3.14.0.75.2,
LCMXO2-1200HC-4SG32C. CP50 подтвердил совпадение native ветви после
препроцессора; CP51 проверил неизменность входов относительно CP50.
Новый synthesis CP50/CP51 не выполнен. Это аудит сохранённых измерений
и новые симуляционные benchmarks, **не уже полученная оптимизация RTL**.

Физически прошит CP29a: 1239 LUT / 326 FF / 6 EBR. Следовательно, запас
**121 LUT** ниже относится к подготовленному MMU-less RTL CP40h/CP50;
у старой прошивки номинально свободен 41 LUT. Плату в этом этапе не меняли.

| Ресурс полного MMU-less board | Занято | Всего | Осталось |
|---|---:|---:|---:|
| LUT4 | 1159 (90,55%) | 1280 | **121** |
| Slices | 584 (91,25%) | 640 | **56** |
| PFU FF | 317 | 1280 | 963 |
| PIO FF | 9 | 66 | 57 |
| EBR | 6 | 7 | **1** |
| PLL | 0 | 1 | 1 |
| PIO sites | 21 + JTAGENB | 22 | **0** |

326 FF из общего MAP report — это 317 PFU + 9 PIO. Свободные FF не являются
эквивалентом свободных LUT: упаковка и routing ограничены уже занятыми slices.
PIO FF нельзя произвольно использовать как внутренний RF. Полный top должен
сохранить существующие multiplexed HG/keyboard/JTAG pin roles.

Из 1159 LUT: **1041 logic, 48 distributed RAM, 70 carry/ripple logic**.
Чтобы достичь <=1100 LUT, нужно убрать минимум 59; для <=1000 — 159.
Снижение только числа FF не решает этот бюджет.

Источники: [MAP](../synth/reports/cp40h/design.mrp),
[TRACE](../synth/reports/cp40h/design.twr),
[source-matched result](../synth/reports/cp40h/result.json).

## Память FPGA

| Назначение | EBR | Содержимое |
|---|---:|---|
| Microstore | 4 | 1024×36, занято 954 слова |
| Opcode dispatch | 1 | 1024×9 |
| SD/RK firmware и RK CSR | 1 | 512×16 payload, два byte ports |
| Свободно | **1** | например, 1024×9 или 512×18 physical bits |

Один EBR позволяет хранить 1 КиБ обычных 16-битных слов; адресная логика,
tags/valid/coherency для cache требуют отдельного бюджета. EBR пока ни под
cache, ни под ODT не резервируется. FIS занимает текущую microstore.

Свободны **70 microinstructions**, но это **46 разрозненных участков**,
максимум **3 слова подряд**. Список uaddresses находится в
[машинном отчёте](resources-cp51.json). Для большой routine потребуется
перекладка микрокода с сохранением OR-dispatch alignment и проверкой labels.

RF16×16 уже находится в distributed RAM: 4 DPR16X4C + 4 SPR16X4C.
Его асинхронные чтения участвуют в однократном ALU execution cycle;
перенос в синхронный EBR меняет расписание, latency и требует отдельного
сравнения. Свободный EBR сам по себе не делает такую замену выгодной.

Установлены 128 КиБ FRAM; native CPU имеет VA=PA16 с I/O page.
Верхние 64 КиБ без MMU не отображаются. Обычная CPU RAM — 56 КиБ до
160000 octal, с прежними private RK physical-copy правилами для I/O page.

## Где находится логика

Это **иерархические Synplify primitive counts**, не независимая стоимость
модулей в MAP LUT. Дочерние блоки включены в родительские; строки не складывать.
Например, ALU/RF включены в datapath, тот — в CPU.

| Блок | ORCALUT4 | PFUMX | CCU2D | EBR |
|---|---:|---:|---:|---:|
| CPU целиком | 582 | 82 | 15 | 5 |
| Datapath, включая ALU/RF | 250 | 65 | 9 | 0 |
| ALU | 98 | 32 | 9 | 0 |
| Microsequencer | 109 | 1 | 6 | 0 |
| Opcode decode с ROM | 51 | 6 | 0 | 1 |
| Board bus со всей периферией | 450 | 36 | 14 | 0 |
| SPI FRAM transport | 84 | 8 | 0 | 0 |
| UART | 72 | 0 | 10 | 0 |
| SD byte engine | 40 | 0 | 4 | 0 |
| KW11 timebase | 9 | 0 | 0 | 0 |
| Panel latch / keyboard / HG | **2** | 0 | 0 | 0 |

HDSP/RGB обслуживаются через panel latch программно. Удаление этой
периферии почти не поможет общей площади. CPU и общая board logic —
существенные части схемы; UART и FRAM также заслуживают отдельных gates.
[Исходный hierarchical report](../synth/reports/cp40h/design.areasrr).

## Что ограничивает частоту

OSCH работает номинально на **29,56 MHz**, FRAM SCK — **14,78 MHz**.
Внутренний TRACE Fmax CP40h — **31,470 MHz**, не измерение FPGA на плате.
External pin delays не заданы. Просто изменить oscillator на 50 MHz нельзя
считать рабочей оптимизацией.

Худший путь: **31,802 ns, 16 logic levels, 61% routing**, slack 2,053 ns:

```text
Microcode EBR
  → dynamic register selector → RF read/address
  → board decode (в отчёте KW11 select) → ACK
  → effective ACK / fault control → microsequencer
  → следующий адрес microcode EBR
```

Поэтому для Fmax надо исследовать общий memory completion path. ALU
не является частью именно этого худшего пути. Дополнительный регистр
может разорвать цепь, но добавляет latency; его пользу следует оценивать
по instructions/sec вместе с wait-state и bus-fault regressions.

## Новые benchmarks полного board

`tb_board_bench_cp51.v` использует текущий `uj11_board` и настоящий RTL
SPI transport с моделью FRAM. Fixture отключает boot overlay и загружает
короткую программу. Регистры устанавливаются гостевыми MOV, затем идут
64 warm retirements и **256 измеряемых инструкций**. Stream проверяется
на каждом retire; для memory MOV дополнительно проверяется результат.

В каждом loop 63 операции и один BR; mixed loop чередует MOV/ADD/BIT/SUB.
BR_self — только BR. Stack loop чередует push/pop и содержит BR; из-за
нечётных 63 операций SP за оборот уменьшается на одно слово. Это короткий
benchmark, не бесконечная программа. Все девять workloads прошли.

| Workload | Microclocks/instruction | Memory beats/instruction | FRAM busy |
|---|---:|---:|---:|
| MOV/ADD/CMP R,R | **107,000** | 1,000 | **96,26%** |
| Mixed register ALU | 107,000 | 1,000 | 96,26% |
| BR self | 107,000 | 1,000 | 96,26% |
| MOV (R3),R2 | 222,172 | 1,984 | 92,00% |
| MOV R1,(R4) | 241,859 | 1,984 | 91,83% |
| MOV (R3),(R4) | 346,203 | 2,969 | 93,44% |
| Stack push/pop | 233,641 | 1,984 | 91,33% |

В register workloads каждая instruction fetch требует нового READ:
**48 SCK**, 96 CPU clocks чистой передачи, **103 clocks transport busy**,
107 clocks суммарно. Расчётная скорость при nominal 29,56 MHz —
**276262 instructions/sec**, это результат RTL simulation, не hardware MIPS.
При такой памяти экономия одного CPU clock изменяет CPI менее чем на 1%.

Cold RT-11FB + DIR из CP50 остаётся отдельной проверкой — 354938300 clocks;
там есть SD/UART polling и IRQ. Его среднее время нельзя переносить на
чистый register loop. Новые loops не проверяют SD boot, IRQ latency,
self-modifying code или новую реализацию cache/prefetch.

## Первый этап оптимизации

1. **Скорость типичного integer code:** отдельный native-board gate для
   последовательного FRAM READ и минимального instruction-stream buffer.
   Сохранять speculative I/O запрет, ROM overlays, RK physical operands,
   invalidation при PC redirect/записи и корректное завершение SPI транзакций.
   Прежний core-only prefetch не является готовой интеграцией полного board.
2. **Площадь:** проверить на MMU-less top shared RX из CP47 и небольшие
   read/decode преобразования по одному. −5 LUT у CP47 относится к MMU
   прототипу и не обещает ту же экономию здесь. CP38 уже показал, что
   массовая замена comparisons на prefixes может увеличить площадь.
3. **Fmax:** после первого memory gate проверить разрыв цепи address→ACK→uPC
   с использованием доступных FF. Сопоставлять добавленные clocks и новый
   реальный TRACE, сохранять bus-error/odd/IRQ поведение.

Первый критерий принятия: полный board fit с периферией, корректность,
измеренное улучшение clocks/throughput и контроль LUT/slices. Желаемые
<=1100 LUT сохраняются. MMU/FP11 не возвращаются, 36-bit store и RF16×16
без доказанной необходимости не расширяются. В CP51 RTL ещё не менялся.

Воспроизведение из `uJ11-fpga`:

```sh
make board
python3 tools/run_board_bench_cp51.py
python3 tools/audit_resources_cp51.py
```

[Counters, source/report hashes](resources-cp51.json),
[raw benchmark logs](../tb/reports/cp51/run.log).
