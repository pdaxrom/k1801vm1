# uJ11

Микрокодный PDP-11/J‑11 для **Lattice MachXO2 LCMXO2-1200HC**.
Рабочий компьютер: 128 КиБ SPI FRAM с отдельными USER/HALT банками,
RT-11FB, SD/RK611, UART, HG и пульт HDSP/20 клавиш.
FIS исполняется микрокодом, FPP J‑11 и ODT — программами в HALT FRAM.
MMU в рабочую сборку не входит.

## Структура

| Каталог | Содержание |
|---|---|
| `rtl/` | Действующий CPU, микросеквенсор, ALU/RF и UART/SPI primitives |
| `boards/hc1200/` | Полный компьютер, FRAM, периферия, RESET и назначение выводов |
| `microcode/` | Единая рабочая микропрограмма `uj11.uasm`, 36 бит |
| `microasm/` | Проверяющий microassembler uJ11 |
| `firmware/` | Исходники bootstrap/resident, UJMOD, ODT и FPP на MACRO-11 |
| `tools/` | Сборка, генерация ROM, тестовые runners, synthesis и UART-инструменты |
| `tests/` | Тестбенчи, независимые эталоны, модели памяти и baseline |
| `demos/rt11/` | BASIC-тесты, FISABI, примеры пульта и HG |
| `releases/` | Проверенный JED и текущие BIN/SAV/REL, manifests и отчёт FPGA |
| `docs/` | Руководства по эксплуатации, устройству и разработке |
| `history/` | Как восстановить прежние эксперименты и их отчёты из Git |
| `build/` | Только результаты сборки; исключены из Git |

Действующий RTL собирается непосредственно из исходников. Старые архивы
и patch-скрипты checkpoint больше не участвуют в сборке.

## Сборка и тесты

Из каталога `uJ11-fpga`:

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
