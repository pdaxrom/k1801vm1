# БК0010-01: положение изображения PAL

Исправлено положение кадра для LCD платы hardware-lcd: изображение сдвинуто
на 8 монохромных пикселей влево и на 4 строки вниз. Все 512×256 пикселей
сохранены. Активное окно: такты 718…3917; строки 32…287 и 345…600.
Предварительная загрузка строкового буфера следует новому вертикальному окну.
Синхронизация и burst PAL из uJ11 не изменены.

- [design.jed](design.jed) — прошивка Flash LCMXO2-7000HC-4TG144I.
- [sources.tar.gz](sources.tar.gz) — переносимый проект Diamond.
- [sd-image.img.gz](sd-image.img.gz) — прежний образ с LOADERBK.BIN и BK0010.ROM.
- [result.json](result.json) — ресурсы и статические тайминги.
- [verification.json](verification.json), [manifest.json](manifest.json) — проверки и SHA-256.
- [Аппаратная сессия](../../research/hardware/20261005-pal/README.md).

Diamond 3.14 на sash@192.168.1.108:
/home/sash/Work/FPGA/bk0010-pal-position-20261005-02.
Все сигналы разведены, **0 setup / 0 hold ошибок**.
Занято 5834/6864 LUT4, 2935/3432 slices, 1940/7209 FF, 9/26 EBR.
Fmax: система 28.661, делитель 57.006, видео 68.833, CPU 27.911 МГц.

[15 fast-проверок](functional/fast.json) прошли с текущими исходниками.
Видеотест сверяет все пиксели в монохромном и цветном режимах за четыре поля,
края окна, scroll/РП, конкуренцию CPU за SRAM и реакцию на остановку памяти.
Дополнительно сверяется независимый монохромный DAC-выход через пять стадий encoder.
[Официальные EBR-модели](functional/mapped-ebr.json) проверили 4096 слов
bootstrap, 64 слова строкового буфера и сохранение содержимого при GSR.

ПЗУ, bootstrap и LOADERBK.BIN не менялись; SD переписывать не требуется.
[30 boot-проверок](../hardware-lcd-loaderbk-20261004/functional/boot.json)
и [полный SoC](../hardware-lcd-loaderbk-20261004/functional/baseline-soc-diamond.json)
сохранены как предыдущие проверки; полный SoC для нового положения кадра
повторно не запускался. Текущий JED проверен записью во Flash и реальной
загрузкой SD: UART B:01…B:07/B:FF, обе проверки CRC32 ПЗУ в SRAM прошли.
Пользователь подтвердил видимость верхней строки загрузчика и правых символов BASIC: «Всё помещается».

JED SHA-256:

```text
64f4e157355c0d4a16a874aaf23e5651dfb25d8546bae57dcb243c900d0120e9
```

Fuse checksum 6086, transmission checksum EED1, 1 441 280 fuse bits.
