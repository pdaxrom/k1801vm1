# Сборка и разработка

Все команды ниже выполняются из `uJ11-fpga`. Исходники этого дерева —
рабочая MMU-less версия компьютера. Нет выбора между десятками `cp*`:
список RTL задаёт [board_common.py](../tools/board_common.py).
Изменения в `build/` будут потеряны; редактировать нужно `rtl/`, `boards/`,
`microcode/` или `firmware/`.

## Зависимости

| Задача | Инструменты |
|---|---|
| Генераторы и unit tests | Python 3, стандартная библиотека |
| Microcode/decoder и unit RTL | Icarus Verilog: `iverilog`, `vvp` |
| Полные RTL-стенды | Verilator, C++ compiler, make |
| Native PDP-11 сборка | SIMH `pdp11`, `../lsi11/rt11tool`, `../lsi11-fpga/images/rt11v503.dsk` |
| BASIC из оригинальной библиотеки | Дополнительно `../lsi11/disks/rt11v5.3/basic.dsk` |
| HC1200 synthesis | Linux Diamond 3.14/Synplify и лицензия |
| Vendor simulation | DP8KC/GSR/PUR/ODDRXE модели Diamond в `.cache/vendor/` |
| UART и прошивка | Linux host, pyserial не нужен; Diamond Programmer и FTDI |

`microasm11` не нужен для текущей сборки и не изменяется.
RT-11 сборщик использует оригинальные MACRO/LINK на частной копии диска;
исходные образы не записываются. Native assembly cache — `build/assembly/`.
Для `vendor` скопировать штатные модели из
`$DIAMOND_HOME/cae_library/simulation/verilog/machxo2/` в `.cache/vendor/`.

## Получение ROM и программ

```sh
make hardware
make software
make verify
```

`build_hardware.py` собирает [uj11.uasm](../microcode/uj11.uasm),
вычисляет dispatch table по [декодеру](../rtl/uj11_decode.v) с проверкой
всех 65536 opcodes, затем собирает native bootstrap/resident/RK/cold walker.

| Выход | Назначение |
|---|---|
| `build/hardware/m0.mem`, `m0.lst`, `m0.labels.json`, `m0.stats.json` | Микрокод и отчёты |
| `build/hardware/decode.mem` | Opcode dispatch |
| `build/hardware/firmware.mem` | Firmware ROM, 1024×16 |
| `build/hardware/uj11_*_ebr.v`, `uj11_decode_table.v`, `uj11_firmware_rom.v` | Vendor EBR initializers |
| `build/software/odt/ODT.BIN` | Отладчик |
| `build/software/sdboot/SDBOOT.BIN` | Заменяемый bootstrap |
| `build/software/loader/UJMOD.SAV` | RT-11 загрузчик |
| `build/fpp/software/FP11.BIN` | Программный FPP |

ODT собирается из отдельных `.MAC` частей и генерируемой таблицы мнемоник.
SDBOOT и UJMOD включают машинные слова bootstrap/helper, собранные из
`SDBASE.MAC` и `HELPER.MAC`; исходные шаблоны имеют расширение `.MAC.in`.
Тестовую программу FPTST можно собрать отдельно:

```sh
python3 tools/rt11_build.py firmware/fpp/FPTST.MAC --out build/fptst-new
```

ROM-only resident занимает адреса ниже SAV header: его слова извлекаются
из MACRO listing с проверкой полного покрытия адресов, а не из заголовка SAV.

`make verify` проверяет совпадение кода RTL/EBR и всех трёх ROM с прошитой
версией, BIN/SAV — с установленными модулями. Это проверка сохранённого
baseline: после намеренного функционального изменения её несовпадение нужно
объяснить новым тестированием и релизом, а не перезаписью прежних hashes.

## Проверки

```sh
make test
make test-fpp-full
make test-rt11 OUT=build/rt11-new
make test-basic OUT=build/basic-new
make test-configuration OUT=build/configuration-new
```

`make test` включает assembler/format/UART unit tests, word/byte ALU, RF/Q,
PSW/memory, кнопку RESET и sequencer,
cold modules/UJMOD, независимые FPP math/conversion models, FPP
smoke/PSW/events и ODT/panel/breakpoints/FP dump.
`test-fpp-full` выполняет полные числовые матрицы sync и logic.
Vendor-прогоны доступны через `tools/test_modules.py --vendor`,
`tools/test_fpp.py --mode vendor`, `tools/test_fpp_psw.py --mode vendor`
и `tools/test_fpp_events.py --vendor`.

Для отдельной BASIC-конфигурации:

```sh
python3 tools/run_basic.py --variant fis --out build/basic-fis-new
python3 tools/run_basic.py --variant single --out build/basic-single-new
python3 tools/run_basic.py --variant double --out build/basic-double-new
```

Исходные `.BAS` хранятся с LF; runner создаёт CRLF-копии для RT-11 в своём
каталоге. Каждый вариант проверяет обычную установку модулей и cold boot.
Make запускает команды последовательно, поскольку генераторы используют
общие ROM и module outputs.

Для полного производственного окна UART ESC:
`python3 tools/test_modules.py --full-window`.
Длинные RT-11-тесты сокращают только счётчики этого ожидания и записывают
подменённые константы в manifest. Они используют SPI FRAM/SD/UART модели.
`test-configuration` создаёт внешний FRAM образ из текущих модулей и таблицы,
затем проверяет обычный cold boot/OFF/повторную установку. Состояние CPU
не подменяется. Для каждого RT-11 прогона нужен **новый каталог**.

Готовые BASIC в `releases/basic/` позволяют запускать регресс без старого
рабочего диска. Сборка BASIC из библиотеки и FISABI:

```sh
python3 tools/build_basic.py --out build/basic-build
python3 tools/build_basic_fis_abi.py --built build/basic-build --out build/basic-adapter
```

## HC1200 synthesis

```sh
make prepare-synthesis OUT=build/hc1200-new
make synthesis OUT=build/hc1200-new
make export OUT=build/hc1200-new
```

Если после prepare нужен synthesis, каталог можно использовать, пока в нём
нет `impl1`. Повторный MAP/PAR — с новым именем.
Target LCMXO2-1200HC-4SG32C; OSCH nominal 29,56 MHz, constraint 31,824 MHz,
внешние FRAM budgets обязательны. `DIAMOND_HOME` по умолчанию
`~/.local/lscc/diamond/3.14`, `DIAMOND_LIBSTDCPP` — Linux compatibility library.
Нужны успешные MAP, полностью разведённый PAR и TRACE. Export проверяет
hashes всех входов и отчётов; сам synthesis не прошивает плату.

## Установка и UART

Для повторения проверенного JED из `releases/hc1200/` на Linux:

```sh
mkdir -p build/program
python3 tools/make_programmer_xcf.py releases/hc1200/design.jed build/program/program.xcf
python3 tools/hardware_uart.py --port /dev/ttyUSB1 --xcf build/program/program.xcf --out build/program/capture
```

Плата включена, JTAG_EN в JTAG; после Verify для клавиатуры/HG переключить
GPIO и физически освободить JTAG pins. Один UART reader: при открытом
picocom использовать актуальный `--pause-pid PID`, не исторический PID.
Helper восстанавливает terminal settings и процесс после завершения.
Режим XCF по умолчанию — FLASH Erase,Program,Verify.

Обычные команды и recovery описаны в [руководстве](user-guide.md).
`--expect-prompt` ожидает RT-11 `\n.`, не ODT/UJMOD. UJMOD принимает одну
команду за запуск. Для HG COPY ждать завершения операции, даже если истёк
таймаут UART helper. `--escape-until-boot 180 --listen 30` записывает recovery:
после ARMED требуется физический длинный RESET.

`make clean` удаляет только генерируемый `build/`; `.cache/vendor`, исходники
и релизы сохраняются. Предыдущие эксперименты — в [Git-истории](../history/README.md).
