# BK0010-01 hardware-lcd — 4 октября 2026

Это историческая первая прошивка с именем LOADER.BIN.
Текущая версия использует отдельный [LOADERBK.BIN](../hardware-lcd-loaderbk-20261004/README.md).

Прошивка для **LCMXO2-7000HC-4TG144I**, кварц 12 МГц, монохромный PAL.
Сборка выполнена Diamond 3.14 на том же Linux-стенде, что uJ11:
`sash@192.168.1.108`, каталог `/home/sash/Work/FPGA/bk0010-20261004-stable-9agkSL`.
Synthesis/MAP/PAR/TRACE завершены; все сигналы разведены, **0 setup / 0 hold ошибок**.

- [design.jed](design.jed) — итоговая прошивка.
- [result.json](result.json) — ресурсы, тайминги, ограничения и контрольные суммы.
- [sources.tar.gz](sources.tar.gz) — переносимый проект Diamond с bootstrap.
- [manifest.json](manifest.json) — SHA-256 файлов пакета и образа SD.
- [Образ SD этой версии](sd-image.img.gz) — сжатые 17 МиБ, FAT16, LOADER.BIN и BK0010.ROM.

ПЗУ загружается с SD через два native-этапа ВМ1. Кнопка переключает
3 → 6 → 12 → 24 МГц; индикатор показывает 03/06/12/24.
Удержание около двух секунд выполняет тёплый сброс с сохранением ПЗУ.
Магнитофон не реализован.

| Ресурс | Занято | Доступно |
| --- | ---: | ---: |
| LUT4 | 5827 | 6864 |
| Slices | 2932 | 3432 |
| FF | 1940 | 7209 |
| EBR | 9 | 26 |

| Такт | Требуется, МГц | Fmax TRACE, МГц |
| --- | ---: | ---: |
| Система | 24 | 32.040 |
| Делитель CPU | 48 | 78.629 |
| Видео | 64 | 68.442 |
| CPU | 24 | 29.165 |

Эти Fmax характеризуют статический анализ FPGA. Обе CPU-фазы проверены
с половиной периода; SRAM/SD I/O и CDC/MAXDELAY ограничения получили
оценки setup и hold. [Основной TRACE](reports/design.twr),
[проверка CPU-фаз](reports/phase.twr), [MAP](reports/design.mrp),
[PAR](reports/design.par), [Synplify](reports/design.srr).
Предупреждения Synplify о black-box PLL/DCCA сохранены в result.json;
такты подтверждены отдельно в физическом TRACE.

Симуляции прошли: [15 быстрых проверок](functional/fast.json),
[26 проверок загрузки и повреждённых образов](functional/boot.json),
[полный SoC с официальной DCCA](functional/soc-diamond.json).
Cold/warm boot, PS/2 `PRINT 2+2`, переключение всех частот и PAL без underrun
проверены через модели SRAM/SD. Все байты ПЗУ и VRAM совпали с C-эталоном.
[PLM](functional/plm-equivalence.json) проверена на 44 768 парах входов
для ВМ1А/ВМ1Г. [Официальные EBR-модели](functional/mapped-ebr.json)
проверили 4096 слов bootstrap, 64 слова строкового буфера и сохранение при GSR
с параметрами и INITVAL итогового [EDIF](reports/synthesis.edi.gz).
Полная post-route SDF-симуляция не выполнялась.

JED: 1 456 106 байт, 1 441 280 fuse bits, fuse checksum `069B`,
transmission checksum `EA61`. SHA-256:

```text
93aad47380391be38ab09874a7e598cc9d5f943542f16bbec8d5d19ba7881836
```

Для повторной сборки распакуйте sources.tar.gz, перейдите в каталог проекта
и выполните:

```sh
env LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 \
    ~/.local/lscc/diamond/3.14/bin/lin64/diamondc build.tcl
python3 tools/diamond_report.py export .
```

`inputs.json` фиксирует входы до сборки; его исходный статус `prepared`
относится к этому этапу. Принятый результат записан в `result.json`.

После формирования пакета Flash прошита и проверена на плате;
SD подготовлена с сохранением файлов Пълдина. Загрузка ПЗУ, обе проверки
CRC32 SRAM и переход в BASIC прошли. [Аппаратные отчёты](../../research/hardware/20261004/README.md)
содержат текущие результаты проверки PS/2, кнопки и частот.
Остаются проверка маркировки FPGA/кварца, измерение PAL под нагрузкой 75 Ом
и калибровка режима 3 МГц относительно реальной БК.
Внешние timing budgets до измерений основаны на параметрах схемы
и предположении о задержках дорожек; детали — [docs/timing.md](../../docs/timing.md).
