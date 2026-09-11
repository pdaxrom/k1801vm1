# CP52 — последовательное чтение FRAM на полном MMU-less компьютере

Эксперимент хранится отдельно, `make board` продолжает собирать CP50/CP40h.
CPU, microcode, FIS, периферия и сохранённая MMU-ветвь не изменены. Плата
остаётся CP29a. Принятие CP52 зависит от нового full-board MAP/PAR/TRACE.

## Протокол и реализация

По документации MR45V100A непрерывный READ увеличивает адрес после каждого
байта, пока CS# остаётся низким. Допустимая частота READ начинается с DC;
SCK можно остановить между словами. Требуемые tSLCH/tSHSL/tCHSH — 10 ns,
tCH/tCL — 13 ns. Источник: LAPIS **FEDR45V100A-01**, стр. 7 и 15,
[документация производителя](https://www.mouser.com/datasheet/2/348/FEDR45V100A-01-1280312.pdf).

`tools/build_fram_cp52.py` получает native ветвь CP50 и формирует две
экспериментальные копии bus/FRAM в `build/cp52-fram`. Добавлены 15-bit
`next_word`, сравнение адреса и продолжение с DATA_LO при совпадении.
Открытый READ отмечается самим выходным FF CS#. При несовпадении CS# поднимается
минимум на один системный такт, затем отправляются новая команда и адрес.
Held-request контракт остался прежним: поля стабильны до ready, перед следующим
запросом нужен sampled req=0. Данные стабильны на ready и между запросами.

Передача следующего слова начинается **только по запросу процессора**.
Speculative prefetch и дополнительный буфер данных здесь ещё не реализованы.
На совпадении требуются 16 SCK вместо 48, экономия 68 CPU clocks с учётом
четырёх пропущенных byte-launch тактов. Частота SPI не повышается.

Квалификация сделана после настоящего board decode. Обычные последовательные
чтения RAM допускаются независимо от IR: это покрывает opcode, immediate,
indexed displacement, absolute address и соседние data words без нового
декодера instruction stream. ROM/CSR-запрос закрывает READ; private RK DMA
не сохраняет его. Writes, transport byte reads, odd words и bank1 никогда
не продолжают READ. Последнее слово bank0 закрывается, поэтому адресный wrap
не может прочитать bank1. Native CPU по-прежнему имеет 16-bit адрес.

Board byte reads, как и в CP40h, возвращают целое выровненное слово, поэтому
следующий адрес транспортного чтения увеличивается на два. Lane selection
остаётся в существующем CPU. Reset/peripheral RESET закрывает CS и сбрасывает
cursor. PC redirect без memory beat не требует отдельного сигнала: непрочитанных
данных нет, а адрес каждого следующего запроса проверяется заново. На HALT/WAIT
без нового запроса READ может оставаться открытым с неподвижным SCK; standby
потребление при таком CS не измерено.

## Проверки

- 4096 случайных операций, обе половины 128 КиБ модели, byte/word/odd,
  held request, полная проверка неизменённых байтов; CLK_DIV=1 и 3.
- 654 направленных операции: 300 последовательных слов через byte-address
  границы в каждом прогоне, возврат/повтор адреса, запись следующего слова,
  byte/odd, bank0 wrap, bank1, закрытие, reset во всех header/data byte phases.
  Отдельный монитор проверяет CS/SCK timing при системном периоде 34 ns.
- Старый board bus тест и расширенный: 30 и 43 beats, ROM/vector boundaries,
  aligned byte, MAINT, UART/SD side effects, RK CSR/vector/physical read/write,
  service overlay release и peripheral reset.
- Все девять CP51 workloads на полном board top; portable и неизменённые
  модели Lattice DP8KC дали одинаковые counters. Каждая retirement проверяет IR.
- Холодная загрузка RT-11FB + DIR: отдельные baseline/candidate прогоны,
  проверка каждого UART wire byte; у candidate каждый принятый FRAM read/write
  дополнительно сравнивается с памятью модели. Оба прогона PASS;
  [source-linked результат](verification-cp52.json), архив `tb/reports/cp52`.

Начальный cold test остановился на промежуточном приглашении SL, ещё до
вывода каталога. FRAM scoreboard воспроизвёл остановку без ошибок данных.
В `tb_board_rt11.v` завершение DIR теперь требует `Files` перед приглашением;
watchdog и проверки SD writeback/IRQ/UART сохранены. Это устраняет зависимость
testbench от относительного времени UART и redraw. Для исходной сборки
получено точное повторение CP50: 354938300 clocks.

## Производительность в симуляции

Каждый тёплый workload: 256 инструкций, loop из 63 операций и BR; отдельно
BR self. Частота в расчёте 29,56 MHz, не измерение на физической плате.

| Workload | CP51 clocks/instruction | CP52 | Ускорение |
|---|---:|---:|---:|
| MOV/ADD/CMP R,R; mixed register ALU | 107,0000 | 40,0625 | 2,671× |
| MOV memory→register | 222,1719 | 222,1719 | 1,000× |
| MOV register→memory | 241,8594 | 241,8594 | 1,000× |
| MOV memory→memory | 346,2031 | 346,2031 | 1,000× |
| BR self | 107,0000 | 107,0000 | 1,000× |
| Stack push/pop | 233,6406 | 233,6406 | 1,000× |

Register loop: 10256 clocks вместо 27392; 4224 SCK вместо 12288;
4 команды READ вместо 256. Расчётно 737847 вместо 276262 instructions/sec.
Последовательное слово без loop redirect занимает 39 clocks/instruction.
Перемежающиеся operand accesses разрывают READ, поэтому memory workloads
не ускоряются. Это ограничение первого небольшого эксперимента.

Холодный **RT-11FB + DIR: 354938300 → 288686609 clocks**, ускорение
**1,229×**, снижение затрат на 18,67%. Одинаковый образ, 300 RK commands,
162 SD reads / 6 writes, 3270 проверенных UART wire bytes. FRAM transactions:
3390712 → 2422032. Timer edges: 576 → 467; выполнение OS и число прерываний
зависят от времени, поэтому сравнение не требует одинакового числа retirements.
Обе raw UART записи сохранены: SL переставляет один из CR относительно LF
при redraw, остальные байты совпадают после удаления CR.

## Ресурсный gate

Подготовлены CP52a — исходный native board и CP52b — последовательный READ.
Target LCMXO2-1200HC-4SG32C, 29,56 MHz, полный CPU/FIS/FRAM/KL11/KW11/
panel/HG/SD/RK/firmware/OSCH/pins. Microcode: прежние 954/1024×36.
Архивный CP40h: **1159 LUT, 326 FF, 6 EBR, 31,470 MHz**. Свободны
121 LUT и один EBR. **LUT/FF/EBR/Fmax нового варианта пока не измерены**;
15 новых RTL bits нельзя подменять результатом MAP. Внешние pin delays
по-прежнему не заданы, TRACE не подтверждает board-level timing.

Воспроизведение локальных проверок:

```sh
python3 tools/run_fram_cp52.py
python3 tools/run_board.py --tag cp52-base
python3 tools/run_board.py --fram-cp52 --tag cp52-final
python3 tools/record_cp52.py
```

Vendor модели должны находиться в `build/vendor`; RT-11 image остаётся
read-only backing, SD writes идут только в RAM overlay модели.
Сборка на сервере с Diamond после разрешённой передачи конкретных исходников:

```sh
python3 tools/checkpoint_board.py cp52a
python3 tools/checkpoint_board.py cp52b --fram-cp52
```

`--prepare-only` формирует проект и input manifest без запуска Diamond.
`--fram-cp52` несовместим с `--mmu`. До получения gate ресурсов эксперимент
не включается в default и не экспортируется для программирования платы.
