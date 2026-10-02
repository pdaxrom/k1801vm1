# Актуальные релизы uJ11

После очистки 2 октября 2026 года здесь остаются готовые комплекты,
которые нужны для установки и воспроизводимой сборки. Промежуточные
прошивки HC7000 и повторные снимки исходников сохранены в локальном Git-архиве;
[исторические отчёты и восстановление](../history/releases/README.md).

| Каталог | Назначение |
|---|---|
| [hc7000-ps2-idle](hc7000-ps2-idle/README.md) | Установленная HC7000 **7CBA**: MMU, FPP off, FIS, SERV, PAL VT52, PS/2; CPU/PAL 50/64 МГц |
| [sd-hc7000-multi](sd-hc7000-multi/README.md) | Полная SD 1 ГиБ: BSD, RSX, RT-11 V4 и XM с дисками компиляторов |
| [hc1200](hc1200/result.json) | Проверенная HC1200 CP67b, JED, исходные входы и отчёты; эталон `make verify` |
| [sd](sd/manifest.json) | SD HC1200 с RT-11FB |
| [software](software/manifest.json) | Программные модули HC1200: ODT, FPP, SDBOOT и загрузчик |
| [hg](hg/manifest.json) | HG, HGTIME и CLOCK для HC1200 |
| [basic](basic/manifest.json) | Проверенные варианты BASIC, используемые сборщиками и тестами |
| [sd-hc7000](sd-hc7000/README.md) | Прежний raw XM-комплект для профиля `IOP=legacy` с FPP; проверенные HGX/HGTIME/TMRATE используются сборщиком новой SD |

Для текущей HC7000 использовать **7CBA + sd-hc7000-multi**.
Raw-образ из `sd-hc7000` не заменяет разделённую SD с несколькими ОС.
Бинарные файлы оставленных комплектов при очистке не изменялись.
`cleanup.json` относится к предыдущей очистке и проверке HC1200;
[новая опись](../history/releases/manifest.json) описывает очистку HC7000.
