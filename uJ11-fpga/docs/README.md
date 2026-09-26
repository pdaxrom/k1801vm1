# Документация uJ11

Рабочий комплект: аппаратная `hc1200-clock-calibrated`, программные модули
формата 4 / ABI3, UJMOD 04.00 и UJBOOT 01.00. Математика FPP — J‑11 CP80.
Модули, ON/AUTO, программные старты и BASIC проверены на плате 2026-09-26;
FPGA не менялась с калибровки таймера 2026-09-23.
HG TIME и foreground CLOCK установлены; [текущий комплект](software.md).

| Задача | Руководство |
|---|---|
| Пользоваться платой и отладчиком | [Эксплуатация](user-guide.md) |
| Записать готовую SD с RT-11FB, утилитами и справкой | [SD-образ](sd-image.md) |
| Выбрать BIN/SAV и проверить STATUS | [Программный комплект](software.md) |
| Синхронизировать RT-11 с хостом, показать часы на HDSP | [HG TIME и CLOCK](host-time.md) |
| Настроить яркость и снизить ток дисплея | [Яркость HDSP](panel-brightness.md) |
| Понять CPU, FRAM, SD/RK и периферию | [Архитектура](architecture.md) |
| Собрать и проверить исходники | [Разработка](development.md) |
| Написать и загрузить модуль | [ABI модулей](modules.md) |
| Выключить/вернуть FPP | [Конфигурация](configuration.md) |
| Проверить математику BASIC | [BASIC](basic.md) |
| Понять FPP и PSW-операнды | [FPP J‑11](fpp.md), [PSW](fpp-psw.md) |
| Смотреть FP-регистры в ODT | [Отладчик FPP](odt-fpp.md) |
| Разобраться с микрокодом | [36-битный формат](microcode-format.md) |
| Написать и собрать микропрограмму | [Микроассемблер: синтаксис, примеры и ошибки](microassembler.md) |
| Подключить клавиатуру | [Клавиши и схема](panel-keyboard.md) |
| Проверить временные ограничения FRAM | [SPI timing](memory-timing.md) |
| Узнать ресурсы и измеренную скорость | [Synthesis](synthesis.md), [измерения](benchmarks.md) |
| Разделить выполненные и будущие проверки | [Verification](verification.md), [TODO](../TODO.md) |
| Найти прежние microcpu/AM4 исследования | [Предыдущие эксперименты](previous-experiments.md) |
| Восстановить старый checkpoint/MMU/отчёт | [История в Git](../history/README.md) |
