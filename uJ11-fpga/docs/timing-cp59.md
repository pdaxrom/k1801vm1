# CP59: timing служебного банка, выбран CP59d

CP59d сохраняет ABI, STEP/SEL004, FIS и весь микрокод CP58a. Полный
LCMXO2-1200HC-4SG32C: **1239 LUT / 343 FF / 6 EBR / 624 slices /
Fmax 32,531 MHz**, 1002 слова микрокода. MAP/PAR/TRACE проходит при
**31,824 MHz**, включая внешние FRAM setup/hold с указанными ниже допущениями.
Штатные CPU и SCK остаются **29,56 MHz**. Это новый opt-in профиль
`--timing-cp59`; default CP52a и установленный CP56a не менялись.

## Почему можно удалить связь bus_fault → CJUMP

В текущем engine поле command выбирает ровно одну микрокоманду. Текущий
`uj11_mem.fault` ненулевой только при активной FETCH/READ/WRITE, командах
2/11/12. Предикат `bus_error` секвенсор потребляет только для CJUMP, команды 1.
Следовательно, именно в момент использования этот предикат всегда нулевой.

В CP59 заменена только связь `.bus_error(bus_fault!=0)` на `.bus_error(1'b0)`.
Настоящие `fault_redirect` и `fault_repair` сохраняют прежний bus_fault.
Ошибки памяти, odd address, прерывания, контекст и число тактов не меняются.
Это свойство текущего fault interface; если позже появится latched fault
или параллельная память, доказательство нужно пересмотреть.

`check_timing_cp59.py` извлекает действительный command/active/fault cone
из frozen RTL CP58a и использует обе копии настоящего microsequencer.
Temporal induction доказала равенство next_address, uPC и link/valid для
произвольных 36-bit uwords, IR, данных и bus inputs, включая ACK/ERROR
вне транзакций. Длина индукции 1. Два повреждения — predicate=1 и снятие
fault_redirect — обнаружены. Это доказательство данной связи, не формальная
верификация всей PDP-11 ISA. Синхронный ROM, RF 16×16 и формат микрокода прежние.

## Четыре реальных gates

| Gate | LUT | FF | EBR | Slices | Fmax MHz | Результат |
|---|---:|---:|---:|---:|---:|---|
| CP58a, основа | 1246 | 342 | 6 | 627 | 30,743 | Только nominal 29,56 MHz |
| CP59a | 1239 | 342 | 6 | 623 | 31,370 | MAP заменил запрошенные 31,824 на 29,56; gate отклонён |
| CP59b | 1239 | 342 | 6 | 623 | 32,165 | Internal 31,824 PASS; отдельный FRAM TRACE: CS setup −1,475 ns |
| CP59c | 1239 | 342 | 6 | 623 | 31,892 | FRAM constraints перед PAR; CS setup −0,703 ns |
| **CP59d** | **1239** | **343** | **6** | **624** | **32,531** | **Internal и FRAM setup/hold PASS** |

Одинаковые LUT в A/B/C не означают одинаковый placement. Gates проходили
в отдельных каталогах; первичные отчёты и точные исходники сохранены.
CP59a `result.json` исправлен на requested-gate FAIL; raw TRACE не изменялся.

MAP требует совпадения OSCH FREQUENCY с NOM_FREQ и подменяет более строгий
LPF clock. Теперь `checkpoint_board.py` оставляет LPF на 29,56 MHz,
после MAP сохраняет `mapped-original.prf`, устанавливает проверочные
31,824 MHz в PRF и только затем запускает PAR/TRACE. Частота самой OSCH
не изменяется. `check_clock` проверяет фактическую preference в raw TRACE;
nominal PASS больше не считается прохождением более строгого gate.
Четыре unit tests включают реальный CP59a и отрицательные случаи.

## Размещение CS и внешняя FRAM

В D к выходу `gpio_mcs` добавлен `syn_useioff=1`, как описано на странице
38 [Lattice Timing Closure 3.11](https://www.latticesemi.com/-/media/LatticeSemi/Documents/UserManuals/RZ2/TimingClosure311.ashx?document_id=45588).
Synplify сохранил внутренний CS FF для обратной связи и продублировал его
в PIO: **334 PFU + 9 PIO registers**. Проверка иерархического EDIF подтвердила
общие D, clock, enable и preset у FD1P3JX и OFS1P3JX; выход второго идёт
на CS pad. Нового такта задержки нет. TRACE подтверждает `IOL_B4C` и
короткий маршрут от I/O-регистра к физическому выводу 8.

Применён прежний [бюджет CP56](spi-cp56.md), теперь до PAR:

```text
INPUT_SETUP PORT "gpio_miso" INPUT_DELAY 23 NS HOLD -2.2 NS CLKNET "clk";
CLOCK_TO_OUT PORT "gpio_msck" MAX 8 NS MIN 2.2 NS CLKNET "clk";
CLOCK_TO_OUT PORT "gpio_mosi" MAX 6.14 NS MIN -6.14 NS CLKNET "clk" CLKOUT PORT "gpio_msck";
CLOCK_TO_OUT PORT "gpio_mcs" MAX 1.14 NS MIN -1.14 NS CLKNET "clk" CLKOUT PORT "gpio_msck";
```

| CP59d constraint | Setup slack ns | Hold slack ns |
|---|---:|---:|
| Internal, 31,824 MHz | 0,683 | 0,293 |
| FRAM MISO | 6,852 | 1,080 |
| SCK absolute envelope | 0,202 | 0,019 |
| MOSI относительно SCK | 6,817 | 6,102 |
| CS относительно SCK | 1,817 | 1,102 |

0,019 ns — запас к выбранной нижней границе SCK clock-to-pad 2,2 ns,
не самостоятельное измерение удержания данных FRAM. Предполагаются PCB
round trip ≤2 ns и MOSI/CS-vs-SCK skew ≤2 ns. Они **не измерены на плате**.
Envelope OSCH +5,5%, duty 43/57 и полный 2% period jitter оставляет короткую
фазу SCK 13,147009 ns при минимуме FRAM 13 ns: всего **0,147009 ns**.
Закрытие setup/hold не доказывает физическую ширину импульсов; её проверка
остаётся открытой. Остальные внешние I/O, перечисленные в raw TRACE как
unconstrained, этим FRAM gate не покрываются.

## Проверки и ресурсы

На окончательных источниках D: 91 CPU scenario × 3 ROM/decode modes,
12 full-board service cases × portable/vendor, 23840 FIS portable + 645
vendor cases, девять benchmarks × 2. Cold RT-11FB + DIR —
**173379163 clocks**, 3270 байт UART побайтно совпадают с CP56.
Полная cold-проверка использует Verilator; vendor EBR/ODDRXE отдельно
проверяются в CPU, board, FIS и benchmark tests через Icarus.

Все гостевые счётчики прежние; MOV/ADD/CMP R,R — **23,5625 CPI**.
Вход в службу 258 clocks, START 120, STEP 119 до запроса следующего opcode,
без завершения самой fetch. При штатных 29,56 MHz это около 1,255 млн
R,R instructions/s по simulation. Fmax вырос, частота платы не повышалась.

Итоговый EDIF: **3091 nets**, нет конфликтующих strong drivers и
необъяснённых floating inputs; 7 CIN доказанно не наблюдаются, три
повреждения netlist обнаружены. MAP warnings о JTAG/GPIO, выключенных
configuration ports и существующем global reset сохранены в отчёте.

MAP: 1111 logic + 48 RF RAM + 80 carry LUT. От CP58a **−7 LUT / +1 FF /
−3 slices / +1,788 MHz**. Свободны **41 LUT / 16 slices / 1 EBR /
22 слова microstore**, PIO нет. До прежней цели ≤1100 LUT ещё 139 LUT.
Критический путь всё ещё EBR→RF→I/O ACK→настоящий fault redirect→EBR:
30,766 ns, 17 уровней, 58,1% routing. Дополнительные аппаратные функции
требуют нового gate; этот результат не даёт большого ресурсного запаса.

## Воспроизведение

Из `uJ11-fpga`, с локальными Icarus/Verilator, vendor models и formal runtime:

```sh
python3 tools/build_timing_cp59.py
python3 tools/check_timing_cp59.py
python3 tools/run_service_cp59.py
python3 tools/run_service_board_cp59.py
python3 tools/check_service_cp59_fis.py
python3 tools/benchmark_service_cp59.py
python3 tools/run_board.py --timing-cp59 --tag cp59d-cold
python3 -m unittest discover -s tb -p test_board_clock.py
```

На Diamond Linux, в новом каталоге реализации (имя ниже пример):

```sh
python3 tools/checkpoint_board.py cp59e --timing-cp59 --clock-mhz 31.824 --fram-timing
```

`tools/record_timing_cp59.py` сверяет CP59d с конкретными тестовыми и
синтезированными исходниками, проверяет CS duplicate и preferences,
сохраняет [verification](verification-cp59.json), [измерения](synthesis-cp59.json),
[raw reports/NCD/EDIF/PRF](../synth/reports/cp59d/) и
[test evidence](../tb/reports/cp59d/). NCD сохранён для воспроизводимого
экспорта; JED не создавался, на плату CP59d не прошивался.

Следующая программная работа — RT-11 `.SAV` loader верхней FRAM с readback,
checksum и установкой ready последним, затем ODT UART/панели и FP11 firmware.
Нереализованные части HALT ВМ2 остаются в [TODO](../TODO.md).
