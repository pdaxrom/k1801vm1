# uJ11

Микрокодный PDP-11/J‑11 для **Lattice MachXO2 HC1200 и HC7000**.
Профиль по умолчанию — HC1200/MMU-less: 128 КиБ SPI FRAM с отдельными
USER/HALT банками, RT-11FB, SD/RK611, UART, HG и пульт HDSP/20 клавиш.
В нём FIS исполняется микрокодом, FPP J‑11 и ODT — программами в HALT FRAM.

На HC7000 установлена **7CBA, 2026-10-02**: MMU, микрокодные FIS/ODT,
FPP выключен, SERV обслуживает диски и экранный терминал PAL 640×240,
PS/2 вводит в системную консоль. CPU/PAL работают на 50/64 МГц;
из 2 МиБ SRAM гостевой ОС доступны 1936 КиБ.
[Текущие ресурсы](docs/synthesis.md#hc7000-установленная-7cba),
[загрузочное меню и диски](docs/boot-validation.md),
[терминал](docs/hc7000-terminal.md), [клавиатура](docs/hc7000-ps2.md).
Готовый [единый SD-образ HC7000](releases/sd-hc7000-multi/README.md)
содержит BSD, RSX, RT-11 V4/XM, компиляторы и HG; после распаковки — 1 ГиБ.

## Структура

| Каталог | Содержание |
|---|---|
| `rtl/` | Действующий CPU, микросеквенсор, ALU/RF и UART/SPI primitives |
| `boards/hc1200/`, `boards/hc7000/` | Платы, память, периферия, RESET и назначение выводов |
| `microcode/` | MMU-less `uj11.uasm` (36 бит) и MMU `uj11-mmu.uasm` (54 бита) |
| `microasm/` | Проверяющий microassembler uJ11 |
| `firmware/` | MACRO-11 bootstrap/модули и C-прошивки SERV, включая `storage/` |
| `tools/` | Сборка, генерация ROM, тестовые runners, synthesis и UART-инструменты |
| `tests/` | Тестбенчи, независимые эталоны, модели памяти и baseline |
| `demos/rt11/` | BASIC-тесты, FISABI, примеры пульта и HG |
| [releases/](releases/README.md) | Актуальные JED, SD-комплекты, BIN/SAV/REL и отчёты FPGA |
| `docs/` | Руководства по эксплуатации, устройству и разработке |
| `history/` | Как восстановить прежние эксперименты и их отчёты из Git |
| `build/` | Только результаты сборки; исключены из Git |

Действующий RTL собирается непосредственно из исходников. Старые архивы
и patch-скрипты checkpoint больше не участвуют в сборке.

## Сборка и тесты

Из каталога `uJ11-fpga`; команды ниже используют HC1200/MMU-less по умолчанию.
Выбор HC7000/MMU описан в [руководстве профиля](docs/hc7000-mmu.md).

```sh
make                    # ROM, ODT, SDBOOT, UJMOD, UJBOOT и FPP
make verify             # совпадение с установленным baseline
make test               # unit, modules, FPP и ODT/panel
make test-rt11 OUT=build/rt11-new
make test-configuration OUT=build/configuration-new
make prepare-synthesis OUT=build/hc1200-new
```

Зависимости и полный порядок: [разработка](docs/development.md).
Для готовой платы доступны [проверенные файлы](docs/software.md).
Ни одна из этих команд не прошивает плату автоматически.

Готовый [SD-образ RT-11FB со всеми утилитами и справкой](docs/sd-image.md)
находится в `releases/sd/uj11-rt11fb.img.gz`. После распаковки записывается
на всю SD-карту, начиная с сектора 0. На самой RT-11: `TYPE SY:README.TXT`.
Для воспроизводимой сборки: `make sd-image SD_OUT=build/sd-new`.

## Документация

- [Устранение задержек консоли 2.9BSD на HC7000](docs/bsd-console.md).
- [Кэш PAR/PDR и ускорение микрокода HC7000 MMU](docs/hc7000-cpu-performance.md).
- [Эксплуатация, RESET, ODT, UART и пульт](docs/user-guide.md).
- [Готовый SD-образ: состав, запись и первый запуск](docs/sd-image.md).
- [Архитектура, карта FRAM, SD/RK и ресурсы](docs/architecture.md).
- [Микроассемблер и примеры микрокода](docs/microassembler.md), [36-битный формат](docs/microcode-format.md).
- [Формат и создание модулей](docs/modules.md), [конфигурация FPP/FIS](docs/configuration.md).
- [Программный FPP J‑11](docs/fpp.md), [BASIC](docs/basic.md).
- [Системное время RT-11 с хоста и часы HDSP](docs/host-time.md).
- [Яркость HDSP и экономия тока](docs/panel-brightness.md).
- [Проверки и их границы](docs/verification.md), [Synthesis](docs/synthesis.md).
- [Оставшиеся задачи](TODO.md), [полный указатель](docs/README.md).

Аппаратный baseline `hc1200-clock-calibrated`: **1244 LUT / 381 FF / 7 EBR**,
TRACE **32,607 МГц**,
OSCH nominal **29,56 МГц**; microstore **1005/1024**. ODT CP77 и FPP CP80
проверены на плате; текущие relocatable-модули, ON/AUTO и программные
перезапуски проверены 2026-09-26.
Калибровка KW11 прошита 2026-09-23; cold boot и автоинициализация модулей прошли.
Номера CP обозначают происхождение релизов; рабочие каталоги названы по назначению.
