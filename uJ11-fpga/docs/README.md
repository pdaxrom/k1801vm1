# Документация uJ11

Актуальность руководств сверена **2026-09-22**. Последняя аппаратная проверка —
[CP82 от 2026-09-20](modules-cp82.md): отключение FPP, FIS BASIC, возврат FPP
и совместная работа FIS/FPU. [Точный состав программного комплекта](software-current.md).

**Установлено 2026-09-19:** [CP80: программный FPP J‑11 с PSW-операндами](board-fpp-cp80.md). SD readback, самоинициализация, 22 PSW/FPS шага, 43 шага регрессии и FPTST — PASS. ODT CP77 сохранён.

Аппаратная система — **CP67b**, HC1200, без MMU, с FIS и двумя банками
FRAM. При cold boot самоинициализируются ODT, SDBOOT и программный FPP.
FPGA прошита 2026-09-13; установка программного CP80 не меняет RTL/микрокод
и не добавляет LUT/FF/EBR. Аппаратные варианты PSW CP78 не прошли MAP
и на плату не устанавливались.

[Семантика и RTL-проверки CP80](fpp-psw-cp80.md),
[файлы для установки и текущие контрольные суммы](software-current.md).

## С чего начать

| Задача | Документ |
|---|---|
| Выбрать актуальные BIN/SAV и сверить таблицу FRAM | [Текущий программный комплект](software-current.md) |
| Пользоваться платой, RESET, UART, ODT и пультом | [Руководство пользователя CP67](user-guide-cp67.md) |
| Понять устройство CPU, банки памяти, EBR, SD/RK и периферию | [Устройство установленной системы](system-cp67.md) |
| Собрать, проверить, синтезировать и прошить | [Разработка и воспроизведение CP67](development-cp67.md) |
| Написать/загрузить модуль, разобраться с checksum и ABI | [Модули HALT FRAM](retained-modules-cp67.md) |
| Узнать, что действительно проверено на плате | [Аппаратный журнал CP67](board-bringup-cp67.md) |
| Проверка UART ESC и возврата ODT | [Успешный аппаратный проход](board-recovery-cp67.md) |
| HG до и после отладки | [Передача файлов до и после ODT](board-hg-odt-cp67.md) |
| Актуальный программный FPP J‑11 | [CP80: PSW-операнды](fpp-psw-cp80.md), [профиль DCJ11](fpp-j11-cp79.md) |
| BASIC с FIS и FPU, готовые программы и проверки | [CP81: результаты](basic-cp81.md), [сборка и запуск](../demos/rt11/basic/README.md) |
| Выключение и возврат FPP; FIS BASIC без адаптера | [CP82: модульные конфигурации](modules-cp82.md) |
| Преобразования FP11-A F/D/I/L | [CP76: семантика, проверки и измерения](fp11-conversions-cp76.md) |
| MODF/MODD в программном FP11-A | [CP75: семантика, проверки и измерения](fp11-mod-cp75.md) |
| MUL/DIV F/D в программном FP11-A | [CP74: алгоритмы, проверки и измерения](fp11-muldiv-cp74.md) |
| Новый FP11 firmware: ADD/SUB F/D, семь guard bits, исправлены ABS/NEG UV | [CP73: семантика, измерения и проверки](fp11-arithmetic-cp73.md) |
| Взять ODT.BIN, FP11.BIN, FPTST.SAV, SDBOOT.BIN и UJMOD.SAV | [Текущие файлы из пакетов CP80/CP67](software-current.md) |
| Узнать оставшиеся задачи | [TODO](../TODO.md) |

Операционные инструкции описывают CP67b с программным обновлением CP80. Документы с номерами
предыдущих CP сохраняют решения и результаты **на момент соответствующего
этапа**. В частности, прежние UJLOAD/UJON, отсутствие автозапуска ODT,
старые роли PREV/NEXT и старые суммы ресурсов не описывают текущую установку.

## Подробности реализации

| Тема | Источники |
|---|---|
| Предыдущие microcpu/microasm/AM4 эксперименты | [previous_experiments](previous_experiments.md) |
| Развитие datapath и формата микрокоманд | [microarchitecture](microarchitecture.md), [microcode-format](microcode-format.md) |
| Адресация, byte/word, ветвления | [addressing-modes](addressing-modes.md), [byte-instructions](byte-instructions.md), [control-flow](control-flow.md) |
| EIS | [MUL](eis-mul.md), [DIV](eis-div.md), [ASH](eis-ash.md), [ASHC](eis-ashc.md), [XOR](eis-xor.md) |
| FIS и программный FPP | [FIS](fis.md), [текущий CP80](fpp-psw-cp80.md), [профиль J‑11 CP79](fpp-j11-cp79.md); история: [CP76](fp11-conversions-cp76.md), [CP75](fp11-mod-cp75.md), [CP74](fp11-muldiv-cp74.md), [CP73](fp11-arithmetic-cp73.md), [CP72](fp11-paths-cp72.md), [CP71](fp11-unary-cp71.md), [CP70](fp11-transfers-cp70.md), [CP69](fp11-memory-cp69.md), [CP68](fp11-firmware-cp68.md), [аппаратный FP11](fp11a.md) |
| Прерывания, ошибки, RTI/RTT | [interrupts](interrupts.md), [memory-faults](memory-faults.md), [trace-rtt](trace-rtt.md) |
| HALT, STEP, кнопка и контекст | [ВМ2: исходное исследование](vm2-halt-reference.md), [CP58](service-bank-cp58.md), [CP63](debug-cp63.md) |
| Резидент копирования и вызов helper | [CP61](halt-boot-cp61.md), [CP62](vector-loader-cp62.md) |
| Автопрокрутка, история команд и точки | [CP65](odt-cp65.md), [CP66](odt-cp66.md) |
| Схема и электрические коды клавиатуры | [panel-keyboard](panel-keyboard.md) |
| FRAM и её ускорение | [первичный аудит](fram-peripherals.md), [CP52](fram-sequential-cp52.md), [CP56](spi-cp56.md) |
| Обработка ошибок SD/RK | [CP60](rk-recovery-cp60.md) |
| Все измеренные варианты | [synthesis](synthesis.md), [benchmarks](benchmarks.md) |

## Где лежат доказательства

- [CP82: модульные конфигурации](../tb/reports/cp82-modules/verification.json): аппаратный OFF/cold/restore, FIS/FPU BASIC, точное восстановление таблицы и полный RTL.
- [CP81: BASIC](../tb/reports/cp81-basic/verification.json): исходные программы, SIMH, RTL, аппаратные журналы и FISABI.
- [Аппаратная установка CP80](../tb/reports/cp80-hardware/deployment.json): SD readback, cold init, 22 PSW/FPS шага, восстановление памяти, 43 STEP и FPTST.

- [Предыдущая установка CP79](../tb/reports/cp79-hardware/deployment.json): SD readbacks, cold init, 43 STEP, FPTST.

- [CP67b synthesis](../synth/reports/cp67b/result.json): MAP/PAR/TRACE,
  исходный manifest и `source.tgz` рядом.
- [Готовый JED](../synth/releases/cp67b/design.jed) и
  [его идентификация](../synth/releases/cp67b/jed.json).
- [Архив simulation и native assembly](../tb/reports/cp67/archive.json).
- [Аппаратный deployment manifest](../tb/reports/cp67-hardware/deployment.json):
  фактическая прошивка, SD readback, cold init, UART R/D/C и hashes журналов.

Release/test manifests и входящие в их hashes README неизменяемы.
`installed_on_board: false` в CP80 — состояние при выпуске пакета;
текущая установка описана выше. Их `programmed: false` фиксирует состояние
до аппаратной установки; последующая прошивка записана в deployment manifest.
Fmax из TRACE, расчётные clocks/second и результаты физической платы имеют
разные источники и не подменяют друг друга.
