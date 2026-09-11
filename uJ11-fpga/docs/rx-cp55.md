# CP55 — shared RX на полном MMU-less board CP54b

Удалён отдельный 8-bit receive shift register FRAM: его работу во время
SPI transfer выполняет `rdata[15:8]`. Полный **CP55a MAP/PAR/TRACE PASS**:
**1182 LUT / 333 FF / 6 EBR / 593 slices / 30,827 MHz**. Относительно
CP54b сэкономлены **3 LUT и 8 FF**, но Fmax ниже на **1,446 MHz**.
Источник — точные FRAM и board-bus файлы из проверенного
`synth/reports/cp54b/source.tgz`.

Принцип уже проверялся в [CP47](area-fram-cp47.md), где shared RX дал
−5 LUT/−8 FF на другой, переполненной MMU-сборке. Эти цифры не переносятся
на native board. В CP55 нет MMU source files, новых операций или speculative
reads. Не повторяются отклонённые CP47 byte-mux/combined варианты.

## Изменение и контракт

- `rx` становится wire alias `rdata[15:8]`.
- На rising SCK MISO сдвигается непосредственно в high byte.
- После DATA_LO high byte копируется в low; при byte transfer high
  обнуляется как прежде. После DATA_HI слово уже готово.
- TX, все состояния/counters, 15-bit cursor, keep/close, SPI pins,
  ready/error/busy и число тактов сохраняются.

**Во время busy high `rdata` намеренно меняется.** Полное слово совпадает
с CP54b на ready и в IDLE/DONE; low byte совпадает каждый такт. Это замена
по контракту потребителя, не равенство всех 16 data bits каждый такт.

Проверен настоящий путь через `uj11_board_bus`, `uj11_core` и `uj11_engine`:
FRAM ACK квалифицирован `fram_ready`; обычный MDR захватывается на
завершении READ, IR/MDR и synchronous opcode ROM — на успешном fetch ACK.
Промежуточные data не разрешают capture. Полные CPU/board tests ниже
используют текущий native core и весь набор периферии, включая private RK.

## Проверки CP55

SAT temporal induction использует проверенный CP47 partial-RX invariant,
расширенный полным native cursor. Доказываются одновременно control state,
счётчики, TX, seen, cursor, все SPI/handshake pins, low data каждый такт,
high data на ready/IDLE/DONE и совпадение уже принятых RX bits по маске.
Начало — нулевое formal state и reset в первом шаге; затем все входы,
включая новые reset, request/address, keep/close и MISO, произвольны.
Это two-state proof; debug outputs/маска существуют только в harness.

| Проверка | Результат |
|---|---|
| Положительный induction, CLK_DIV=1/3 | 2 PASS |
| Byte high / cursor alias mutations | 2 formal rejects и 2 исполнимых counterexamples |
| Four-state pin/data miter, CLK_DIV=1/2/3 | 585 beats, 768 reset offsets, 199780 clock comparisons |
| Независимая модель FRAM, полный 128 КиБ scoreboard | 6144 random операций |
| Sequential READ/redirect/byte/odd/write/bank/reset/hold/CS/SCK timing | 981 направленная операция |
| Board overlays/MAINT/byte lanes/KW11/UART/SD/RK/vector/DMA/RTI/panel | 43 beats |
| Full-board portable + unmodified Lattice EBR models | 9 + 9 workloads, counters CP54b совпали |
| Новый cold RT-11FB + DIR | PASS, 288686609 clocks |

В X/Z miter неизвестные значения подаются на serial MISO и transmitted
payload при известных controls. Включены 64 последовательных чтения,
переход `000000 → 100002` (octal), explicit close и reset sweep. Negative
cursor-control действительно ловит неверное продолжение READ при совпадении
младших 14 word-address bits; отказ formal дополнен конкретной SPI ошибкой.
Strict standalone Verilator lint и итоговые Icarus/Verilator builds чистые.

Cold FB: **300 RK commands, 467 timer edges, 3270 UART wire bytes,
162 SD reads/6 writes**. Retired **3979364**, reads **5207588**, writes
**422214**, FRAM transactions **2422032**. Все counters и raw UART точно
совпали с CP54b. Каждый принятый FRAM beat проверен по содержимому модели.
Образ `lsi11-fpga/images/rt11v503.dsk` не изменён, SHA256
`e769228f2e1262220297bfa98b8f2841688849ab4c49ad9cd48d0d73d0a99553`.
Это MMU-less FB, не новый RT-11XM test.

R,R workloads остаются **40,0625 CPI**, memory/stack/BR — без изменения
счётчиков. CPU/FIS, 954 microinstructions, board decode/ACK, остальные
устройства и сохранённая MMU-ветвь не менялись. FIS corpus заново не
запускался: ни ISA/datapath, ни valid memory data contract не изменились.

[Manifest и hashes](verification-cp55.json), raw logs и source snapshot —
`tb/reports/cp55`. Это сохранённый отчёт локальной фазы до synthesis;
финальные измерения находятся в [synthesis-cp55.json](synthesis-cp55.json).

## Synthesis

После явного разрешения пользователя **7 файлов, 48012 байт** переданы на
`sash@192.168.1.108:/tmp/uj11-cp55-20260911`. Остальные 37 inputs скопированы
из CP54 после проверки hashes. Архив передачи — 13946 байт, SHA256
`147439cab6e799453452259765a59a91dcad3582ea681ce0e7994e01264010ad`.
Согласованный manifest, исходники до/после сборки и все соответствующие
HDL локальных tests точно совпали. Дисковые образы и `microasm11` не передавались.

Выполнен **один полный gate CP55a** для LCMXO2-1200HC-4SG32C, 29,56 MHz,
с CPU/FIS/FRAM/KL11/KW11/panel/HG/SD/RK/firmware/OSCH/reset/pins.
Diamond 3.14.0.75.2 / Synplify V-2023.09L-2; полностью разведён,
MAP/PAR/TRACE PASS, microcode **954/1024×36**.

| Gate | LUT4 | FF | EBR | Slices | Fmax, MHz | Slack, ns |
|---|---:|---:|---:|---:|---:|---:|
| CP52a, default | 1159 | 326 | 6 | 584 | 31,470 | 2,053 |
| **CP54b, выбран пользователем** | **1185** | **341** | **6** | **595** | **32,273** | **2,843** |
| CP55a, эксперимент | 1182 | 333 | 6 | 593 | 30,827 | 1,390 |

После сравнения пользователь выбрал **CP54b основой дальнейшей оптимизации**.
Экономия 3 LUT и 8 FF у CP55a не оправдывает уменьшение запаса timing
с 2,843 до 1,390 ns. В обоих вариантах штатная частота остаётся 29,56 MHz;
32,273 MHz — расчётный внутренний Fmax CP54b, не новая частота платы.
CP55a сохранён как эксперимент; shared RX не переносится в следующие
изменения. Для работы с выбранным CP54b используется `--ack-cp54 dma-ack`.

У выбранного **CP54b** остаются **95 LUT / 45 slices / 1 EBR**, свободных
PIO sites нет. Цена sequential READ относительно default — **26 LUT /
15 FF / 11 slices**. До цели <=1100 LUT нужно убрать ещё **85 LUT**.
Эксперимент CP55a оставляет 98 LUT / 47 slices / 1 EBR. Default остаётся
CP52a; физическая плата CP29a. В этом checkpoint плату не прошивали.

### Mapping и критический путь

MAP: **1054 logic + 48 distributed RAM + 80 carry LUT**. У CP54b было
1057/48/80. PFU FF уменьшились с 333 до 326, PIO FF — с 8 до 7.
EDIF подтверждает удаление ровно семи `rx[7:1]` PFU-регистров и одного
`rx[0]` PIO-регистра; новых последовательных ячеек нет. FRAM сохраняет
пять CCU2D для сравнения cursor. Проверены **2980 сетей EDIF**, несколько
сильных драйверов на одной сети не обнаружены. INOUT nets записаны отдельно;
это не проверка физического contention.

Synplify hierarchical ORCALUT4 у FRAM: 109 → 98, у engine: 526 → 540
при неизменённом CPU RTL. Глобальное преобразование и packing перераспределяют
логику; эти counts не заменяют конечную экономию **3 LUT всего компьютера**.

Худший путь: EBR lane 2 → dynamic RF → **address[0] → request/write →
service_dma_operand → cpu_io_page/ACK → bus fault/fault_redirect →
microsequencer → EBR lane 2**. Задержка **32,465 ns**, 17 уровней,
60,3% routing, slack **1,390 ns**. Класс пути не изменился; меньшая площадь
FRAM не сократила этот путь. Один PAR run не отделяет вклад mapping от
placement/routing; seed sweep не выполнялся.

Коды и число выведенных Synplify warnings совпали с CP54b. BN161 ограничен
100 сообщениями, это не полный счётчик случаев. MAP сохранил три прежних
предупреждения: JTAG/GPIO, configuration ports и local timer reset; ошибок
нет. Raw reports, source snapshot и сжатый EDIF — `synth/reports/cp55a`.

Внешние pin delays не заданы: **Fmax относится только к внутреннему timing**.
Первый бит MISO теперь захватывается существующим PFU result-регистром,
а не отдельным PIO RX-регистром. Физический запас FRAM input timing этим
gate не измерен. Локальные tests завершены до synthesis; после совпадения
source hashes повторные cold/vendor прогоны не требовались.

## Воспроизведение

```sh
python3 tools/build_rx_cp55.py
python3 tools/check_rx_cp55.py
python3 tools/run_rx_cp55.py
python3 tools/run_board.py --tag cp55-final --rx-cp55
python3 tools/record_rx_cp55.py
python3 tools/checkpoint_board.py cp55a --rx-cp55
python3 tools/archive_synthesis.py cp55a
python3 tools/record_synthesis_cp55.py
```

Synthesis требует Diamond на сервере; `--prepare-only` создаёт проект без
запуска Diamond. Выше перечислены выполненные команды, повторные gates
требуют новых имён. Для повторного аудита EDIF восстанавливается из
`synth/reports/cp55a/design.edi.gz` в `build/cp55-netlist/cp55a_impl1.edi`.
`--rx-cp55` взаимоисключающий с MMU и другими experimental board profiles.
