# Исторические релизы HC7000

2 октября 2026 года из `releases/` удалены 33 промежуточные версии HC7000:
5176 файлов, 132,84 МиБ содержимого. Все удалённые файлы побайтно сверены
с сохранённой версией Git до удаления:

**`31549d1378faa4546b66cc8dd50f325cea6a26ad`**.

Здесь сохранены небольшие описания этапов и отчёты, на которые ссылается
документация. Это исторические записи, а не комплекты для прошивки.
Старые JED, образы дисков, копии исходников и полные журналы остаются
в указанной версии локального Git-архива. Текущий комплект —
[HC7000 7CBA](../../releases/hc7000-ps2-idle/README.md) и
[записи разметки SD](hc7000-sd-compilers/README.md).
[Опись](manifest.json) содержит исходные пути, размеры, идентификаторы
деревьев Git и SHA-256 сохранённых записей.

## Открыть или восстановить старую версию

Старые каталоги удалены также из истории ветки `uJ11-hc7000`.
GitHub и второй Git-сервер больше не используются для их восстановления.
Полная прежняя история сохранена локально в отдельном Git bundle,
который не публикуется в репозитории:

`uJ11-fpga/build/git-release-purge-20261002-01/k1801vm1-before-release-purge.bundle`.

Bundle содержит все прежние локальные ветки и исходную версию `31549d1`.
Для восстановления использовать отдельный каталог, из корня репозитория:

```sh
git clone uJ11-fpga/build/git-release-purge-20261002-01/k1801vm1-before-release-purge.bundle ../uj11-history-hc7000
git -C ../uj11-history-hc7000 checkout --detach 31549d1378faa4546b66cc8dd50f325cea6a26ad
git -C ../uj11-history-hc7000 show 31549d1:uJ11-fpga/releases/hc7000-serv-19k/README.md
```

Сохранить bundle вне временного `build/`, прежде чем удалять этот каталог.
В свежем клоне с remote этого локального архива нет. Генерируемый `build/`,
vendor tools и внешние дисковые исходники в bundle не входят.

## Эталоны тестов

Два теста читали данные из старых релизов. Их входы перенесены без изменения
байтов в `tests/baseline/hc1200-serv.json` и
`tests/baseline/pal-cpu-perf.json`; пути в тестах обновлены.
Сборка и проверки не требуют восстановления исторических каталогов.

## Сохранённые этапы

| Этап | Запись |
|---|---|
| Первая HC7000 и SERV | [HC7000](hc7000/README.md), [SERV](hc7000-serv/README.md) |
| MMU и FPP | [Микрокод](hc7000-mmu-microcoded/README.md), [TRAP](hc7000-mmu-trap-fix/README.md), [FPP off](hc7000-mmu-nofpp/README.md) |
| Дисковые контроллеры | [IOP](hc7000-serv-io/README.md), [UNIBUS](hc7000-serv-unibus/README.md) |
| Частота и CPU | [50 МГц](hc7000-50mhz/README.md), [PAR/PDR](hc7000-par-pdr-cache/README.md), [микрокод](hc7000-microcode-return/README.md) |
| Память SERV | [Общая RAM](hc7000-serv-shared/README.md), [19 КиБ](hc7000-serv-19k/README.md) |
| PAL и терминал | [PAL](hc7000-pal-preview/README.md), [прокрутка](hc7000-terminal-scroll/README.md), [вывод](hc7000-terminal-throughput/README.md), [VT52](hc7000-terminal-vt52/README.md) |
| PS/2 | [Первый этап](hc7000-ps2/README.md) |
| Запись разделов SD | [RT-11 V4](hc7000-sd-rt11v4/README.md), [XM и компиляторы](hc7000-sd-compilers/README.md) |
