# Документация uJ11

**Установлено 2026-09-15:** [программный FPP J‑11 CP79 и ODT с FP-просмотром](board-fpp-cp79.md). SD readback, самоинициализация, 43 аппаратных STEP и FPTST — PASS.

Аппаратная система — **CP67b**, HC1200, без MMU, с FIS и двумя банками
FRAM. При cold boot самоинициализируются ODT, SDBOOT и программный FPP.
FPGA прошита 2026-09-13; установка программного CP79 не меняет RTL/микрокод
и не добавляет LUT/FF/EBR. Аппаратные варианты PSW CP78 не прошли MAP
и на плату не устанавливались.

Подготовлен программный [CP80: PSW-операнды FPP](fpp-psw-cp80.md),
[пакет](../demos/rt11/service/cp80/README.md). Он ещё не установлен на плату.

## С чего начать

| Задача | Документ |
|---|---|
| Пользоваться платой, RESET, UART, ODT и пультом | [Руководство пользователя CP67](user-guide-cp67.md) |
| Понять устройство CPU, банки памяти, EBR, SD/RK и периферию | [Устройство установленной системы](system-cp67.md) |
| Собрать, проверить, синтезировать и прошить | [Разработка и воспроизведение CP67](development-cp67.md) |
| Написать/загрузить модуль, разобраться с checksum и ABI | [Модули HALT FRAM](retained-modules-cp67.md) |
| Узнать, что действительно проверено на плате | [Аппаратный журнал CP67](board-bringup-cp67.md) |
| Проверка UART ESC и возврата ODT | [Успешный аппаратный проход](board-recovery-cp67.md) |
| HG до и после отладки | [Передача файлов до и после ODT](board-hg-odt-cp67.md) |
| Актуальный программный FPP J‑11 | [CP79: профиль DCJ11 и проверки](fpp-j11-cp79.md) |
| Преобразования FP11-A F/D/I/L | [CP76: семантика, проверки и измерения](fp11-conversions-cp76.md) |
| MODF/MODD в программном FP11-A | [CP75: семантика, проверки и измерения](fp11-mod-cp75.md) |
| MUL/DIV F/D в программном FP11-A | [CP74: алгоритмы, проверки и измерения](fp11-muldiv-cp74.md) |
| Новый FP11 firmware: ADD/SUB F/D, семь guard bits, исправлены ABS/NEG UV | [CP73: семантика, измерения и проверки](fp11-arithmetic-cp73.md) |
| Взять ODT.BIN, FP11.BIN, FPTST.SAV | [Пакет CP79](../demos/rt11/service/cp79/README.md) |
| Взять SDBOOT.BIN и UJMOD.SAV | [Неизменённый пакет CP67](../demos/rt11/service/cp67/README.md) |
| Узнать оставшиеся задачи | [TODO](../TODO.md) |

Операционные инструкции описывают CP67b с программным обновлением CP79. Документы с номерами
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
| FIS и программный FP11 | [FIS](fis.md), [FP11 firmware CP76](fp11-conversions-cp76.md), [CP75](fp11-mod-cp75.md), [CP74](fp11-muldiv-cp74.md), [CP73](fp11-arithmetic-cp73.md), [CP72](fp11-paths-cp72.md), [CP71](fp11-unary-cp71.md), [CP70](fp11-transfers-cp70.md), [CP69](fp11-memory-cp69.md), [CP68](fp11-firmware-cp68.md), [история аппаратного FP11](fp11a.md) |
| Прерывания, ошибки, RTI/RTT | [interrupts](interrupts.md), [memory-faults](memory-faults.md), [trace-rtt](trace-rtt.md) |
| HALT, STEP, кнопка и контекст | [ВМ2: исходное исследование](vm2-halt-reference.md), [CP58](service-bank-cp58.md), [CP63](debug-cp63.md) |
| Резидент копирования и вызов helper | [CP61](halt-boot-cp61.md), [CP62](vector-loader-cp62.md) |
| Автопрокрутка, история команд и точки | [CP65](odt-cp65.md), [CP66](odt-cp66.md) |
| Схема и электрические коды клавиатуры | [panel-keyboard](panel-keyboard.md) |
| FRAM и её ускорение | [первичный аудит](fram-peripherals.md), [CP52](fram-sequential-cp52.md), [CP56](spi-cp56.md) |
| Обработка ошибок SD/RK | [CP60](rk-recovery-cp60.md) |
| Все измеренные варианты | [synthesis](synthesis.md), [benchmarks](benchmarks.md) |

## Где лежат доказательства

- [Аппаратная установка программного CP79](../tb/reports/cp79-hardware/deployment.json): SD readbacks, cold init, 43 STEP, FPTST.

- [CP67b synthesis](../synth/reports/cp67b/result.json): MAP/PAR/TRACE,
  исходный manifest и `source.tgz` рядом.
- [Готовый JED](../synth/releases/cp67b/design.jed) и
  [его идентификация](../synth/releases/cp67b/jed.json).
- [Архив simulation и native assembly](../tb/reports/cp67/archive.json).
- [Аппаратный deployment manifest](../tb/reports/cp67-hardware/deployment.json):
  фактическая прошивка, SD readback, cold init, UART R/D/C и hashes журналов.

Release/test manifests неизменяемы. Их `programmed: false` фиксирует состояние
до аппаратной установки; последующая прошивка записана в deployment manifest.
Fmax из TRACE, расчётные clocks/second и результаты физической платы имеют
разные источники и не подменяют друг друга.
