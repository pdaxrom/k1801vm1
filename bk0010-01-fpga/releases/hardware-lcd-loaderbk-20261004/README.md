# БК0010-01: отдельный LOADERBK.BIN

Загрузчик БК теперь называется **LOADERBK.BIN**. Первичный EBR bootstrap
ищет только это имя, поэтому он работает на общей карте с LOADER.BIN Пълдина.
Второй этап остался прежним: 337 байт; MONIT10/BASIC10 не изменены.
Первичный bootstrap изменён только в двух байтах имени FAT 8.3.

- [design.jed](design.jed) — прошивка Flash для LCMXO2-7000HC-4TG144I.
- [sources.tar.gz](sources.tar.gz) — переносимый проект Diamond.
- [sd-image.img.gz](sd-image.img.gz) — сжатый SD-образ с LOADERBK.BIN и BK0010.ROM.
- [result.json](result.json) — принятые ресурсы и тайминги.
- [manifest.json](manifest.json) — SHA-256 всех файлов.
- [Аппаратные отчёты](../../research/hardware/20261004-loaderbk/README.md).

Diamond 3.14 на `sash@192.168.1.108` выполнил Synthesis/MAP/PAR/TRACE
в `/home/sash/Work/FPGA/bk0010-loaderbk-20261004-01`.
Все сигналы разведены; **0 setup / 0 hold ошибок**.
Занято 5827/6864 LUT4, 2932/3432 slices, 1940/7209 FF, 9/26 EBR.
Fmax: система 32.040, делитель 78.629, видео 68.442, CPU 29.165 МГц.

[30 проверок загрузки](functional/boot.json) прошли: штатная/фрагментированная
FAT16, SDHC/SDSC-v1, повреждённые образы, warm reset и сверка ПЗУ/VRAM
с C-эмулятором. Новый тест загружает БК при наличии чужого LOADER.BIN первым
в root; отсутствие LOADERBK.BIN отвергается с E:31.
[EBR-модели](functional/mapped-ebr.json) проверили все 4096 слов нового
bootstrap, 64 слова строкового буфера и сохранение при GSR.
Предыдущие fast/SoC/PLM результаты сохранены как baseline: RTL не изменён.
Полный SoC повторно не запускался; изменённый bootstrap проверен boot-тестами,
моделями EBR и аппаратным запуском.

JED SHA-256:

```text
a74dfb81c832517571c26a56085d270402f5bb41b25b31b5b68daf568074b6c1
```

Fuse checksum `070B`, transmission checksum `EA61`, 1 441 280 fuse bits.
На физической карте прежний 481-байтовый LOADER.BIN Пълдина восстановлен
побайтно; LOADERBK.BIN и BK0010.ROM находятся рядом с файлами Пълдина.
Другие файлы, MBR и разделы сохранены.

Измерение уровней PAL и калибровка абсолютной скорости CPU остаются
отдельными проверками. Инструкции сборки — [README проекта](../../README.md).
