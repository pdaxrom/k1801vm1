# CP52 — последовательное чтение FRAM на полном MMU-less компьютере

Эксперимент хранится отдельно, `make board` продолжает собирать CP50/CP40h.
CPU, microcode, FIS, периферия и сохранённая MMU-ветвь не изменены. Плата
остаётся CP29a. Оба новых full-board MAP/PAR/TRACE прошли, но CP52b
сохранён отдельно до уменьшения площади и улучшения timing margin.

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

Выполнены CP52a — исходный native board и CP52b — последовательный READ.
Target LCMXO2-1200HC-4SG32C, 29,56 MHz, полный CPU/FIS/FRAM/KL11/KW11/
panel/HG/SD/RK/firmware/OSCH/pins. Microcode: прежние 954/1024×36.

| Full-board gate | LUT4 | FF | EBR | Slices | Fmax MHz | Setup slack ns |
|---|---:|---:|---:|---:|---:|---:|
| CP52a, baseline | 1159 | 326 | 6 | 584 | 31,470 | 2,053 |
| CP52b, sequential READ | 1198 | 341 | 6 | 603 | 30,044 | 0,544 |
| Изменение | +39 | +15 | 0 | +19 | −1,426 | −1,509 |

Оба gates полностью routed, timing PASS на 29,56 MHz. CP52a повторил все
показатели CP40h. У CP52b остаются **82/1280 LUT, 37/640 slices, 1/7 EBR**;
PIO sites заняты. FF: 333 PFU + 8 PIO вместо 317 + 9; поэтому прирост
total FF +15 не означает одинакового распределения регистров по site types.

MAP: logic LUT 1041→1054, carry LUT 70→96, distributed RAM остаётся 48.
В иерархии FRAM Synplify: ORCALUT4 84→95, PFUMX 8→9, CCU2D 0→13.
Эти primitive counts не складываются с MAP LUT4 и не являются отдельными
независимыми оценками модулей. Увеличение carry usage указывает на стоимость
формирования/сравнения cursor; разделить вклад инкремента и comparator
нужно отдельным experiment, по этому отчёту они не разделены.

Новый critical path: microstore lane2 → dynamic RF selector/read → address →
service/board decode → ACK → CPU step/IRQ acknowledge → UART RX IRQ FF.
Delay 32,830 ns, 19 logic levels, 57,7% routing. Сам cursor отсутствует
в этом худшем пути, поэтому уменьшение Fmax нельзя приписать только ему.
Внешние pin delays по-прежнему не заданы; TRACE не подтверждает board-level timing.

**Решение:** сохранить CP52b как измеренный performance candidate. До
включения в default уменьшить площадь и восстановить запас timing; цель
<=1100 LUT пока не достигнута. Следующий локальный эксперимент — уменьшение
стоимости cursor, затем native read/decode/ACK. Microcode и FIS сохранены,
дополнительный EBR не занят, MMU не развивался.

Проверенные raw reports и source archives находятся в `synth/reports/cp52a`
и `synth/reports/cp52b`; [машинный audit](synthesis-cp52.json) связывает
их с проверенными в simulation исходниками. `tools/record_synthesis_cp52.py`
проверяет все source/report hashes и неизменность native baseline.

Воспроизведение локальных проверок:

```sh
python3 tools/run_fram_cp52.py
python3 tools/run_board.py --tag cp52-base
python3 tools/run_board.py --fram-cp52 --tag cp52-final
python3 tools/record_cp52.py
```

Vendor модели должны находиться в `build/vendor`; RT-11 image остаётся
read-only backing, SD writes идут только в RAM overlay модели.
Выполненные команды на сервере с Diamond после разрешённой передачи исходников:

```sh
python3 tools/checkpoint_board.py cp52a
python3 tools/checkpoint_board.py cp52b --fram-cp52
```

`--prepare-only` формирует проект и input manifest без запуска Diamond.
`--fram-cp52` несовместим с `--mmu`. При повторении нужны свободные checkpoint
имена, существующие reports не перезаписываются. Эксперимент не включён
в default и не экспортирован для программирования платы.
