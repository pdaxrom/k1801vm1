# Документация uJ11

Актуальная установленная система — **CP67b**, HC1200, без MMU, с FIS,
двумя банками FRAM и самоинициализирующимися ODT/SDBOOT. Аппаратная установка
и проверка выполнены 2026-09-13, коммит `a600155`.

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
| Новый FP11 firmware: управление и переносы LDF/LDD/STF/STD, пока без арифметики | [CP70, проверки и ограничения](fp11-transfers-cp70.md) |
| Взять готовые ODT.BIN, SDBOOT.BIN, UJMOD.SAV | [Пакет для RT-11](../demos/rt11/service/cp67/README.md) |
| Узнать оставшиеся задачи | [TODO](../TODO.md) |

Операционные инструкции выше описывают CP67b целиком. Документы с номерами
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
| FIS и программный FP11 | [FIS](fis.md), [FP11 firmware CP70](fp11-transfers-cp70.md), [CP69](fp11-memory-cp69.md), [CP68](fp11-firmware-cp68.md), [история аппаратного FP11](fp11a.md) |
| Прерывания, ошибки, RTI/RTT | [interrupts](interrupts.md), [memory-faults](memory-faults.md), [trace-rtt](trace-rtt.md) |
| HALT, STEP, кнопка и контекст | [ВМ2: исходное исследование](vm2-halt-reference.md), [CP58](service-bank-cp58.md), [CP63](debug-cp63.md) |
| Резидент копирования и вызов helper | [CP61](halt-boot-cp61.md), [CP62](vector-loader-cp62.md) |
| Автопрокрутка, история команд и точки | [CP65](odt-cp65.md), [CP66](odt-cp66.md) |
| Схема и электрические коды клавиатуры | [panel-keyboard](panel-keyboard.md) |
| FRAM и её ускорение | [первичный аудит](fram-peripherals.md), [CP52](fram-sequential-cp52.md), [CP56](spi-cp56.md) |
| Обработка ошибок SD/RK | [CP60](rk-recovery-cp60.md) |
| Все измеренные варианты | [synthesis](synthesis.md), [benchmarks](benchmarks.md) |

## Где лежат доказательства

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
