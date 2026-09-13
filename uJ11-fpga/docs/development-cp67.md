# Разработка и воспроизведение CP67

Это инструкция для **CP67b `--modules-cp67`**, установленной на HC1200.
[Пользовательские операции](user-guide-cp67.md), [устройство](system-cp67.md),
[ABI модулей](retained-modules-cp67.md), [аппаратный журнал](board-bringup-cp67.md).
Все shell-команды ниже выполняются из **корня репозитория k1801vm1**,
если явно не указано другое. Новому прогону давать новый каталог/имя.

## Какие исходники действительно попадают в сборку

Проект развивался отдельными synthesis checkpoints. Корневые `rtl/`,
`boards/hc1200/` и `microcode/` содержат базовые/экспериментальные варианты;
они не являются сами по себе списком исходников установленного JED.
Фактический список и SHA256 —
[synth/reports/cp67b/inputs.json](../synth/reports/cp67b/inputs.json),
точные входные файлы — соседний `source.tgz`.

| Источник/инструмент | Роль |
|---|---|
| [build_modules_cp67.py](../tools/build_modules_cp67.py) | Проверяет/извлекает frozen CP63b, добавляет cold ROM и формирует CP67 hardware sources |
| [CP63b archive](../synth/reports/cp63b/source.tgz) и [manifest](../synth/reports/cp63b/inputs.json) | Точный прежний CPU, RF/ALU, microstore, decode, button, FRAM и периферия |
| [firmware/cp67/BOOT.MAC](../firmware/cp67/BOOT.MAC) | Cold walker и ROM-процедура его установки |
| [build_software_cp67.py](../tools/build_software_cp67.py) | Собирает ODT/SDBOOT/UJMOD, упаковывает ABI3 |
| [firmware/odt](../firmware/odt/) | Общие ODT, дизассемблер, панель, история, STEP OVER, точки и данные |
| [ODINIT.MAC](../firmware/cp67/ODINIT.MAC) | Новый инициализатор ODT, очистка/проверка mutable state, включение debug |
| [SDBOOT.MAC.in](../firmware/cp67/SDBOOT.MAC.in) | Шаблон заменяемого bootstrap с resident-вызовом HALT→USER |
| [UJMOD.MAC.in](../firmware/cp67/UJMOD.MAC.in) | Шаблон RT-11 загрузчика; использует проверенные функции/helper CP62 |
| [module_image_cp67.py](../tools/module_image_cp67.py) | Header, bounds, checksum, pack/decode модулей |
| [rt11_build.py](../tools/rt11_build.py) | Настоящие DEC MACRO/LINK в RT-11 под SIMH |
| [microasm/uj11asm.py](../microasm/uj11asm.py) | 36-битный microassembler v12, включая service/space, listing, labels, occupancy |
| [checkpoint_board.py](../tools/checkpoint_board.py) | Выбор профиля, точный Diamond project, MAP/PAR/TRACE и manifest |

`build_modules_cp67.py` оставляет CPU/микрокод CP63b прежними, меняет адресацию
firmware ROM/overlay и создаёт два byte-wide EBR вместо одного прежнего
firmware EBR. Сборка проверяет SHA архивных входов и ожидаемые места изменений.
В `build/cp67-modules/` появляются `src/rtl`, `src/boards/hc1200`,
`src/microcode/generated`, `m0.mem/lst/labels/stats`, `service.uasm`,
`decode.mem`, `firmware.mem`, `boot-symbols.json`, `inputs.json`.

Для изменения CPU требуется осознанно изменить **источник выбранного нового
профиля**, согласованные assembler extensions/ROM/decode и создать новый
checkpoint. Редактирование генерируемого `build/` будет потеряно при сборке.
Архивы успешных CP63b/CP67b не исправляются задним числом. Текущий uj11asm
понимает `service`/`space`/`d=STATUS`, но сам не добавляет HALT routines
в исходный `m0.uasm`: нужен полный согласованный `service.uasm` профиля.

Текущий `microasm11` — отдельный проект: CP67 его не меняет и не использует.
MMU-прототип сохранён, но здесь флаг `--mmu`/define `UJ11_MMU` отсутствует.
`-DUJ11_MMU=0` тоже нельзя использовать для отключения `ifdef`: сам факт
определения макроса включает условный код.

## Зависимости

| Задача | Что требуется |
|---|---|
| Проверка архивов/формата | Python 3, стандартная библиотека |
| Native PDP-11 assembly/link | SIMH `pdp11` в PATH; собранный `lsi11/rt11tool`; `lsi11-fpga/images/rt11v503.dsk` с MACRO/LINK |
| Portable RTL/ODT/RT-11 simulation | Verilator, C++ compiler, make |
| Vendor ROM simulation | Icarus Verilog (`iverilog`, `vvp`), Lattice DP8KC/GSR/PUR/ODDRXE models в `uJ11-fpga/build/vendor/` |
| HC1200 synthesis | Linux Lattice Diamond 3.14 с Synplify и действующей лицензией |
| Прошивка | Diamond Programmer 3.14, FTDI JTAG, включённая плата в JTAG mode |
| HG | `hgfsd`, libftdi1 и USB access на Linux; `HG.SYS` на RT-11 диске |

Базовый диск assembly берётся из `lsi11-fpga/images/rt11v503.dsk`, а не из
прежнего MMU/XM эксперимента `disks/rt11v5.3/system.dsk`. Скрипт работает
на частной копии `build.dsk` и записывает hashes источников/результатов.
Готовые BIN/SAV/JED можно установить без повторной native assembly/synthesis.

## Собрать software и проверить имеющийся release

```sh
python3 uJ11-fpga/tools/verify_modules_cp67.py
python3 uJ11-fpga/tools/build_modules_cp67.py
python3 uJ11-fpga/tools/build_software_cp67.py
```

Первая команда проверяет **архив**: SHA, успешные отчёты и согласованность
JED/software; она не запускает тесты заново и не опрашивает плату.
Её `programmed: false` — сохранённое до установки состояние release.
Фактическая прошивка отражена в [deployment.json](../tb/reports/cp67-hardware/deployment.json).
`--current` дополнительно сравнивает текущие файлы с архивными, включая
сгенерированные выходы; несовпадение после намеренной правки не исправляется
перезаписью старого manifest.

Результат software build:

| Путь под `build/cp67-software/` | Содержание |
|---|---|
| `odt/ODT.BIN` | Файл установки ABI3 |
| `odt/UJMON.MAC`, `odt/image.bin`, `odt/result.json` | Итоговый source, immutable bytes, символы/размеры |
| `sdboot/SDBOOT.BIN` | Заменяемый bootstrap |
| `loader/UJMOD.MAC`, `loader/UJMOD.SAV` | Итоговый source и RT-11 executable загрузчика |

Assembly-каталоги с hash в имени содержат DEC OBJ/SAV/LST/MAP, console.log,
build-inputs.json. Старый cache используется только после проверки hashes.
`payload.bin` ODT — подготовленное состояние для регрессионных стендов;
для установки нужен именно `ODT.BIN` с ABI3 header. Файлы `demos/.../cp67`
— опубликованный release; новая сборка сама его не заменяет.

Чтобы отдельно получить listing/labels/statistics уже подготовленного полного
микрокода, после `build_modules_cp67.py`:

```sh
python3 uJ11-fpga/microasm/uj11asm.py uJ11-fpga/build/cp67-modules/service.uasm -o uJ11-fpga/build/cp67-ucode-new/m0.mem --list uJ11-fpga/build/cp67-ucode-new/m0.lst --labels uJ11-fpga/build/cp67-ucode-new/labels.json --stats uJ11-fpga/build/cp67-ucode-new/stats.json
cmp uJ11-fpga/build/cp67-ucode-new/m0.mem uJ11-fpga/build/cp67-modules/m0.mem
```

Assembler проверяет поля, разрядности, пересечения `.org`, OR-dispatch mask,
переходы в незаполненные адреса и допустимую fault continuation. Статистика
routine — статический размер, не динамический CPI. В microasm обычные числа
десятичные, `$...`/`0x...` — HEX, `0o...` — OCT; в DEC MACRO-11 числа без
суффикса обычно восьмеричные. Эти синтаксисы нельзя смешивать.

Изменение только ODT/SDBOOT/UJMOD проверяется на уровне PDP-11 кода и FRAM:
число LUT/FF/EBR от размера BIN не меняется. Изменение BOOT.MAC в EBR или RTL
уже требует нового аппаратного checkpoint, timing и JED.

## Проверки по слоям

После сборки, выбирая нужные проверки по изменению:

```sh
python3 uJ11-fpga/tools/test_module_image_cp67.py
python3 uJ11-fpga/tools/test_loader_functions_cp67.py
python3 uJ11-fpga/tools/test_modules_cp67.py
python3 uJ11-fpga/tools/test_modules_cp67.py --vendor
python3 uJ11-fpga/tools/test_modules_cp67.py --full-window
python3 uJ11-fpga/tools/test_odt_cp64.py --odt uJ11-fpga/build/cp67-software/odt --out uJ11-fpga/build/cp67-core-new
python3 uJ11-fpga/tools/test_odt_cp66.py --odt uJ11-fpga/build/cp67-software/odt --out uJ11-fpga/build/cp67-bp-new
python3 uJ11-fpga/tools/test_odt_cp65.py --cp66 --odt uJ11-fpga/build/cp67-software/odt --out uJ11-fpga/build/cp67-panel-new
python3 uJ11-fpga/tools/run_modules_cp67.py --out uJ11-fpga/build/cp67-rt11-new
```

Суффиксы `cp64/cp65/cp66` у ODT test runners обозначают происхождение стенда;
аргумент `--odt` подаёт **новый CP67 образ**, `--cp66` выбирает текущее меню/T.
Эти направленные тесты используют прежний проверенный CPU и модель памяти.
Полный `run_modules_cp67` проверяет новый board path с UART wire/SPI FRAM/SD
и настоящей RT-11. Его каталог должен быть новым.

| Архивный результат CP67 | Объём | Итог |
|---|---|---|
| Формат файла | 6 tests | PASS |
| Native loader/helper | 38 cases, 49 checks | PASS |
| Cold start, portable и vendor | По 30 cases, 249 checks | PASS, одинаковые clocks |
| Производственное окно ESC | 1 case, 7 checks; 64 768 442 clocks до первого USER `004000` | PASS |
| ODT core | 187 checks, 175 commands | PASS |
| Breakpoints | 112 checks, 103 commands | PASS |
| Panel/navigation | 2699 checks, 163 commands, 85 windows | PASS |
| Полная RT-11 | 55 checks, 1 023 462 941 clocks, 3983 UART bytes | PASS |

Длинные функциональные прогоны сокращают только literal-счётчики ожидания
ESC; отдельный `--full-window` исполняет производственные значения. Общая
длительность многоразовой загрузки/тестирования не является CPI benchmark.
[Первичные логи, assembly и hashes](../tb/reports/cp67/archive.json).

На плате отдельно подтверждены FLASH Verify, RT-11FB/DIR, полный обратный COPY
трёх файлов, оба cold init без повторной загрузки, короткий RESET и ODT R/D/C.
Отдельно прошёл [аппаратный ESC/обычный RESET/возврат ODT](board-recovery-cp67.md),
с проверкой таблицы и RT-11 DIR. Отключение питания посреди записи, внедрение
зависшего модуля и полный проход матрицы в CP67-сеансе ещё не квалифицированы;
не заменять это PASS симулятора.

## Synthesis и готовый JED

На Linux с Diamond, из корня checkout:

```sh
python3 uJ11-fpga/tools/checkpoint_board.py cp67new --modules-cp67 --clock-mhz 31.824 --fram-timing
python3 uJ11-fpga/tools/export_board.py cp67new
```

`cp67new` — пример свежего имени, не повторное использование существующего
`impl1`. Для одной подготовки проекта добавить `--prepare-only` к первой
команде; export требует завершённого успешного synthesis. Профиль задавать
явно: default без `--modules-cp67` соответствует прежнему CP52a.

По умолчанию Diamond ищется в `~/.local/lscc/diamond/3.14`, переопределение —
`DIAMOND_HOME`. Linux compatibility libstdc++ задаётся `DIAMOND_LIBSTDCPP`,
если установленный путь отличается. Target — LCMXO2-1200HC-4SG32C,
top — `uj11_hc1200_microcomp`, pins — [pins.lpf](../boards/hc1200/pins.lpf).
31,824 MHz — timing constraint при OSCH nominal 29,56 MHz; обязательны
внешние FRAM budgets. Оценивать MAP **и** fully routed PAR **и** TRACE.

`export_board.py` проверяет source hashes и отчёты перед ExportJedecgen;
это не повторный synthesis и не прошивка. Новый результат —
`build/cp67new/impl1/cp67new_impl1.jed` и `build/cp67new/jed.json`.

Проверенный установленный файл: [synth/releases/cp67b/design.jed](../synth/releases/cp67b/design.jed).
SHA256 `d2e9000c47477c0ad8e8b4167338c74f274f2650dc46d8f1378446bb467e710d`,
JEDEC checksum `84CC`; input revision SHA256
`d8669ab5a2779f05492c361ac1e27fa3fc2ad99b0e41c3112f8c905817ed8cc6`.
CP67b: 1244 LUT / 381 FF / 7 EBR / 628 slices / Fmax 32,032 MHz,
1005 microinstructions. Свободны 36 LUT / 12 slices / 0 EBR / 19 uwords.
Это тесный fit, а не прежняя целевая оценка 900–1000 LUT.

## Прошивка и UART-журнал

На подключённом Linux host остановить свой HG, освободить UART reader,
подключить JTAG, выбрать JTAG_EN и убедиться, что плата включена. Для
повторения **готового CP67b** без изменения содержимого JED:

```sh
mkdir -p uJ11-fpga/build/cp67-program-new
python3 uJ11-fpga/tools/make_programmer_xcf.py uJ11-fpga/synth/releases/cp67b/design.jed uJ11-fpga/build/cp67-program-new/program.xcf
python3 uJ11-fpga/tools/hardware_uart.py --port /dev/ttyUSB1 --xcf uJ11-fpga/build/cp67-program-new/program.xcf --out uJ11-fpga/build/cp67-program-new/capture
```

XCF по умолчанию задаёт `FLASH Erase,Program,Verify`, FTUSB-0/DUAL RS232 A.
Другому адаптеру нужны соответствующие `--port`/`--usb-id` XCF generator.
UART helper вызывает Programmer из
`~/.local/lscc/programmer/diamond/3.14/bin/lin64/pgrcmd`, сохраняет программатор
и UART логи. Для проверки только ID у XCF есть `--operation "FLASH Verify ID"`.

Если UART уже открыт picocom, определить **актуальный PID владельца** и
передать `--pause-pid PID` helper вместо второго одновременного reader.
Скрипт проверяет открытый дескриптор, временно приостанавливает только выбранный
процесс, затем восстанавливает termios и возобновляет его в finally.
PID и `/tmp/...` из аппаратного журнала — исторические, не постоянная настройка.

После Verify проверить баннер RT-11/DIR, затем перевести GPIO и физически
освободить JTAG pins. Установленные FRAM-модули CP67 могут уже активироваться;
если нужна их замена, использовать [инструкцию UJMOD](user-guide-cp67.md).

Пример записи одной RT-11 команды при свободном UART:

```sh
python3 uJ11-fpga/tools/hardware_uart.py --out uJ11-fpga/build/cp67-dir-new --command DIR --expect-prompt --command-wait 30
```

`--expect-prompt` ждёт возврат **RT-11 `\n.`**, а не `UJMOD>` или `ODT>`.
Для входа `RUN UJMOD` использовать отдельный вызов без этого флага, дождаться
и проверить `UJMOD>`, затем послать STATUS/имя. Завершение команды UJMOD
возвращает RT-11, и там флаг применим. Без флага наличие UART bytes само
по себе не доказывает выполнение: проверить содержание журнала/приглашение.
Для HG COPY использовалось `--command-wait 300`; после timeout нельзя
продолжать отправку до подтверждения окончания предыдущей команды.

Для аппаратной проверки ESC есть отдельный режим (на Linux с подключённой
платой, актуальным PID терминала либо свободным UART):

```sh
python3 uJ11-fpga/tools/hardware_uart.py --out uJ11-fpga/build/cp67-recovery-new --escape-until-boot 180 --listen 30
```

После `[ARMED]` сделать длинный RESET. Скрипт посылает ESC каждые 100 ms
до **нового** баннера RT-11FB V05.03, затем прекращает передачу и пассивно
записывает загрузку. Ранее принятый баннер/эхо ESC/одна точка не завершают
ожидание. Через 180 секунд без баннера скрипт завершится с ошибкой и вернёт
UART терминалу. Режим нельзя сочетать с `--command`, `--interrupt` или XCF;
он сам не нажимает RESET и не объявляет проверку recovery успешной только
на основании загрузочного баннера. Дополнительно проверить обход ODT и
возвращение его автозапуска после следующего обычного cold reset.
`session.json` содержит `recovery_attempt` с числом ESC и результатом ожидания.

Транспортные проверки на псевдотерминале, без платы:

```sh
python3 uJ11-fpga/tools/test_hardware_uart.py
```

## HG и заимствованная периферия

Панельный драйвер/font/keymap основан на
[PNLDRV.MAC](../demos/rt11/panel/PNLDRV.MAC). HG использует существующие
[host/hg](../../lsi11-fpga/host/hg/) и
[hostdisk documentation](../../lsi11-fpga/demos/rt11/hostdisk/README.md)
из `lsi11-fpga`; это отдельные общие компоненты, а не новый FPGA peripheral.
Host `Makefile` использует libftdi1 через pkg-config и общий `lsi11/tools/rt11fs.c`.

На Linux с собранным hgfsd, в отдельном терминале:

```sh
lsi11-fpga/host/hg/hgfsd --directory /path/to/cp67-share --clock 1000
```

Здесь `/path/to/cp67-share` заменить на каталог с готовыми тремя файлами.
Внутри directory mode создаёт backing volume; результаты COPY обратно
извлекаются в этот каталог. На RT-11 нужны LOAD HG/COPY/UNLOAD HG, затем
завершить именно свой daemon. UART, HG и JTAG — разные соединения; успешный
UART не доказывает, что общие HG/keyboard pins освобождены.

## Что фиксировать при следующем изменении

Для программного модуля: source/assembly hashes, BASE/immutable length/full
allocation, BIN header/checksum, проверки команд и cold init, обратную копию
файла с SD, UJMOD STATUS до/после cold reset. Для hardware изменения дополнительно:
полный source manifest, LUT/FF/EBR/slices/Fmax, clocks и условия измерения,
MAP/PAR/TRACE, согласованный JED и фактический Programmer Verify.
Обновлять [synthesis](synthesis.md), [benchmarks](benchmarks.md),
[implementation status](implementation-status.md) по виду изменения;
не выдавать размер software за новую оценку ресурсов FPGA.
