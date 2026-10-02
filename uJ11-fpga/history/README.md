# История экспериментов

Отдельно выполнена [очистка промежуточных релизов HC7000](releases/README.md)
2 октября 2026 года. Их полная версия сохранена в локальном Git-архиве `31549d1`;
текущие [релизы для установки](../releases/README.md) перечислены отдельно.

До упорядочивания дерево содержало 9228 отслеживаемых файлов: исходники
разных поколений, `cp*`, экспериментальные варианты, промежуточные релизы,
более восьми тысяч файлов synthesis/test reports. Последняя полная версия:

**`3d4c8d5b026852adc262c9a5fac2953377f95962`**.

Она сохранена в Git. Здесь нет второй копии этого дерева и нет зависимости
текущей сборки от его извлечения. Рабочие исходники материализованы из точных
входов прошитой CP67b, firmware — из ODT CP77/FPP CP80/UJMOD/SDBOOT ABI3.
Функциональное поведение и готовые BIN/JED при очистке не меняются.

## Открыть старый документ или отчёт

Из корня репозитория:

```sh
git show 3d4c8d5:uJ11-fpga/docs/synthesis.md
git show 3d4c8d5:uJ11-fpga/docs/previous_experiments.md
git show 3d4c8d5:uJ11-fpga/tb/reports/cp82-modules/verification.json
```

## Восстановить полный checkpoint-проект

Использовать свободный каталог, не поверх текущего проекта:

```sh
git worktree add --detach ../uj11-history 3d4c8d5b026852adc262c9a5fac2953377f95962
```

В нём работают прежние пути/README/Makefile и hash-verifiers. Генерируемый
`build/`, vendor tools и внешние дисковые образы восстанавливаются по старой
инструкции; Git их не содержит. Отдельный `microasm11` не изменяется.

| Материалы | Путь в исторической версии |
|---|---|
| Все LUT/FF/EBR/Fmax эксперименты | `uJ11-fpga/docs/synthesis.md`, `synth/reports/` |
| Benchmarks AM4/microcpu/uJ11 | `docs/previous_experiments.md`, `docs/benchmarks.md` |
| MMU 18/22, RT-11XM, варианты размещения | `rtl/experimental/`, `docs/mmu-*.md`, `tools/*cp3*.py` … `*cp49*.py` |
| Отложенный CPU-local PSW | `rtl/cp78/`, `synth/reports/cp78*/` |
| Все старые тесты, логи, source snapshots | `tb/reports/` и `tools/verify_*.py` |
| История ODT/FPP/загрузчика | `demos/rt11/service/cp*/`, `firmware/cp*/` |
| Аппаратные журналы | `docs/board-*.md`, `tb/reports/cp*-hardware/` |
| BASIC и цикл FPP OFF/restore | `tb/reports/cp81-basic/`, `tb/reports/cp82-modules/` |

[Карта перемещённых файлов](path-map.json) помогает найти новое назначение.
Она описывает перенос исходников, а не совместимые aliases старых CLI.
Текущие [команды сборки](../docs/development.md) и [ограничения](../TODO.md)
не нужно искать по номерам checkpoint.
