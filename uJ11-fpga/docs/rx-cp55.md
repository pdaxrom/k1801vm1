# CP55 — shared RX на полном MMU-less board CP54b

Удалён отдельный 8-bit receive shift register FRAM: его работу во время
SPI transfer выполняет `rdata[15:8]`. Это **−8 RTL state bits**, но итоговые
LUT/FF/Fmax пока не измерены. Источник — точные FRAM и board-bus файлы из
проверенного `synth/reports/cp54b/source.tgz`; CP54b занимает **1185 LUT /
341 FF / 6 EBR / 595 slices / 32,273 MHz**.

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
`tb/reports/cp55`. Default остаётся CP52a, лучший измеренный sequential
candidate — CP54b; физическая плата CP29a.

## Synthesis

Подготовлен **один полный gate CP55a** для LCMXO2-1200HC-4SG32C, 29,56 MHz,
с CPU/FIS/FRAM/KL11/KW11/panel/HG/SD/RK/firmware/OSCH/reset/pins.
Новые LUT/FF/EBR/Fmax пока отсутствуют: эффект отдельно взятого удаления
RX register нельзя объявлять экономией полной сборки без MAP/PAR/TRACE.

Автопроверка отклонила transfer CP55, посчитав прежнее согласие ограниченным
CP54. Запрошено отдельное подтверждение: **7 файлов, 48012 байт**
(RTL, scripts, manifests; `/tmp/cp55-files.txt`) на
`sash@192.168.1.108:/tmp/uj11-cp55-20260911`, затем один full synthesis.
Передача не состоялась. Остальные проверенные inputs уже находятся на
сервере в CP54 и копируются только после проверки hashes. Дисковые образы
и `microasm11` в пакет не входят. До измерения ресурсов кандидат не принят.

## Воспроизведение

```sh
python3 tools/build_rx_cp55.py
python3 tools/check_rx_cp55.py
python3 tools/run_rx_cp55.py
python3 tools/run_board.py --tag cp55-final --rx-cp55
python3 tools/record_rx_cp55.py
python3 tools/checkpoint_board.py cp55a --rx-cp55 --prepare-only
```

Synthesis требует Diamond на сервере; `--prepare-only` убирается после
разрешённой передачи. Повторные gates требуют новых имён. `--rx-cp55`
взаимоисключающий с MMU и другими experimental board profiles.
