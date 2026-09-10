# CP43 — MMR3 CSR в экспериментальном APR board

**MMR3 доступен CPU по VA 172516, с корректными word/byte accesses и RESET.**
Выбран CP43d: **1258 LUT / 351 FF / 7 EBR / 630 slices / 30.866 MHz**.
Это +10 LUT / +7 FF относительно CP42d, без новых EBR и microinstructions.
Свободны **22 LUT / 10 slices / 0 EBR**. Полный MAP/PAR/TRACE прошёл при
29.56 MHz на LCMXO2-1200HC-4SG32C, Diamond 3.14.0.75.2.

Это отдельная экспериментальная сборка: `build_mmr3_cp43.py` добавляет
CSR к CP42 APR board. Native production CP40h остаётся 1159 LUT / 326 FF /
6 EBR / 31.470 MHz и 954 words. Плата остаётся CP29a; прошивки здесь нет.
MMR0/1/2, CPU translation, PDR checks/automatic W, abort/restart, PA22 и
верхние 64 КиБ FRAM для CPU ещё не подключены. RT-11XM пока не загружен.

## Контракт по DEC

Источник: локальный `../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf`, §4.7.4,
§4.9 register map и flowchart RESET. Доступная
[OCR-копия руководства DCJ11](https://dusted.dk/pages/computers/J-11/datasheet-DEC-DCJ11_Microprocessor_Users_Guide_OCR.pdf).
Canonical physical CSR — `17772516`; unmapped VA — `172516`, старший byte
— `172517`. Недокументированный alias `177516`, присутствующий в C emulator
для прежних конфигураций, в RTL не добавляется.

| Бит | Назначение DEC | Реализация CP43 |
|---:|---|---|
| 15:6 | Reserved, read zero | Читаются как ноль, запись игнорируется |
| 5 | Enable I/O map, MAP output | Хранение; MAP output не подключён |
| 4 | Enable 22-bit mapping | Хранение; CPU translation ещё отсутствует |
| 3 | Enable CSM instruction | Хранение; CSM ещё не реализован |
| 2 / 1 / 0 | Kernel / Supervisor / User D space | Хранение; lookup остаётся kernel unified |

Шесть младших битов программируются word или low-byte записью. High-byte
и нулевая byte mask их не меняют. INIT и kernel RESET очищают MMR3; RESET
не очищает APR RAM. В полном MMU бит 4 имеет значение только при MMR0<0>=1:
0 выбирает 18 bits, 1 — 22 bits. Сам по себе MMR3 в CP43 не включает MMU.
Processor modes/privilege сейчас ограничены прежним kernel-only CPU.

## Хранение, handshake и isolation

MMR3 хранится в шести FF модуля `rtl/experimental/uj11_mmr3.v`. Ещё один FF
подтверждает запрос после тактового фронта. ACK держится до снятия request
и запрещает повторную запись принятого запроса. Master сохраняет controls
и data до ACK и даёт один фронт с request=0 между обращениями. Этот контракт
уже выполняет turnaround полного board. Чтение не имеет побочных эффектов.

Шестибитный CSR не требует нового порта/reset sequence APR EBR. Вариант
хранения MMR3 в EBR в CP43 не реализован и не измерен; экономия от него не
предполагается. Неизменённый shared APR port остаётся отдельным устройством.

Board decoder использует `cpu_io_page`, а не только младшие адресные биты.
Физические RK service operands при совпадении численного адреса уходят
в FRAM. CSR не запускает FRAM/UART/SD/firmware/panel/timer и не выбирает APR.
PA22 decoder появится вместе с физическим интерфейсом; текущая проверка
всех VA не выдаётся за PA22 integration.

## Измеренные варианты полного HC1200

| Gate | Подтверждение / read mux | LUT4 | FF | EBR | Slices | TRACE MHz | Words |
|---|---|---:|---:|---:|---:|---:|---:|
| CP42d | APR baseline | 1248 | 344 | 7 | 625 | 30.896 | 963 |
| [CP43a](../synth/reports/cp43a/result.json) | Pulse ACK + seen, masked read | 1269 | 352 | 7 | 636 | 30.901 | 963 |
| [CP43b](../synth/reports/cp43b/result.json) | Immediate ACK, masked read | 1271 | 350 | 7 | 637 | 30.958 | 963 |
| [CP43c](../synth/reports/cp43c/result.json) | Immediate ACK, shared MMR3/MAINT read | 1269 | 350 | 7 | 639 | 29.583 | 963 |
| [CP43d](../synth/reports/cp43d/result.json) | Held registered ACK, masked read — выбран | 1258 | 351 | 7 | 630 | 30.866 | 963 |

Все четыре gates прошли fitter/timing. D выигрывает по площади и имеет
запас относительно частоты платы. B убирает два FF, но LUT растут; C почти
исчерпывает timing margin. Меньшее количество FF не гарантирует меньшую
площадь полного FPGA. Raw отчёты и исходники каждого варианта сохранены.
External pin delays не заданы; TRACE Fmax не является измерением на плате.

## Проверки финального D

- Unit: 1030 checked edges, все 64 значения, read/write/lane combinations,
  удержание request, reset при активной записи и приоритет reset.
- Strict Verilator `--Wall` для нового модуля, без waivers.
- Differential с существующим DCJ11 C core: **262160 команд**, включая
  **16 RESET**, все 65536 значений × четыре lane masks. Использованы
  `mmu_io_write_word`, `mmu_io_write_byte`, `mmu_io_read_word` и
  `dcj11_reset_instruction_state`; это CSR oracle, не CPU translation oracle.
- Полная board bus: **2097152** адресных/DMA комбинаций, все 65536 word
  values через настоящий read mux, **131115 beats** вместе с существующими
  UART/KW11/panel/SD/RK/FRAM проверками. Physical DMA write не меняет MMR3.
- Actual CPU + SPI FRAM, portable и unmodified vendor DP8KC: **324 readbacks /
  516 MMR3 beats / 3508 lookup reads / 157988 clocks**, все значения,
  MOV/MOVB, high-byte zero, RESET и сохранность APR. Отдельно odd word read
  и write: vector4, отсутствует failed CSR beat, прежнее значение сохраняется.
- Прежний APR CPU test на обеих ROM-моделях: **432 readbacks / 720 beats /
  4902 lookup reads / 221988 clocks**, все 48 пар, byte lanes, vector4,
  сохранность APR после reset. Результат точно совпадает с CP42.

MMR3 CSR в D требует одного дополнительного такта против immediate ACK B:
разница directed workload составляет 516 clocks на 516 CSR beats. Это
не меняет lookup helper: всё ещё +10 clocks на memory word без hold.
Microstore эксперимента прежний: 954 guest + 9 helper words, ширина 36 bits.
Engine, ALU, RF, FIS, decoder, microcode и firmware здесь не изменялись.
Предыдущий полный FIS corpus не перезапускался; его CP42 evidence сохранён.

Cold RT-11FB + DIR прошёл с **412130048 clocks / 3987390 retirements**,
5222610 reads / 424452 writes, 3397976 FRAM transactions, 300 RK commands,
663 timer edges, 3270 UART bytes и 162 SD reads / 6 overlay writes. Все counts
и UART transcript побайтно совпадают с CP42. Backing FB/XM images проверены
по SHA256 и не изменены; записи SD testbench идут в RAM overlay.

[Verification manifest](verification-cp43.json) связывает точные исходники,
raw synthesis reports и проверки. Наличие MMR3 не подтверждает загрузку XM
или готовность полной MMU. Icarus сохраняет прежние предупреждения CP42
о forward declaration running/advance и inherited UART timescale; новых
предупреждений CSR нет. Strict `--Wall` относится к новому модулю, полная
board simulation дополнительно проверяет width warnings как fatal.

## Воспроизведение

Из `uJ11-fpga`, с существующими generated ROMs и Lattice models:

```sh
python3 tools/build_mmr3_cp43.py
python3 tools/check_mmr3_cp43.py unit
python3 tools/check_mmr3_cp43.py oracle
python3 tools/check_mmr3_cp43.py bus
python3 tools/check_mmr3_cp43.py cpu
python3 tools/check_mmr3_cp43.py cpu --vendor
python3 tools/check_mmr3_cp43.py apr
python3 tools/check_mmr3_cp43.py apr --vendor
python3 tools/run_mmr3_cp43_board.py
python3 tools/checkpoint_mmr3_cp43.py cp43d
python3 tools/archive_synthesis.py cp43d
python3 tools/record_cp43.py
```

Diamond gate требует свежий implementation directory. Для исторических
A/B/C нужны их `source.tgz`: форма ACK изменилась между gates. Существующие
архивы не перезаписываются. `microasm11` и sibling projects не изменяются.

Следующий шаг — измеренный gate MMR0 enable и microcoded relocation с
сохранением VA/MDR/Q до завершения FRAM. При запасе 22 LUT расширять RTL
нужно вместе с сокращением общей логики. Дальше обязательны PDR/abort/restart,
PA22 с NXM до усечения к FRAM17, extended DMA и проверка именно RT-11XM.
