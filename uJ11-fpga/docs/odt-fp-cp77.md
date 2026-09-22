# CP77: FP11 в ODT и новая раскладка HALT FRAM

Это результаты этапа CP77. Позднее этот ODT установлен вместе с
[FPP J‑11 CP79](board-fpp-cp79.md), затем FPP обновлён до CP80.
[Текущий комплект и размеры модулей](software-current.md); размеры FP11
в таблицах ниже относятся к прежнему профилю CP77.

ODT показывает состояние программного FP11 и дизассемблирует его команды.
Все изменения — обычный PDP-11 код во FRAM; FPGA CP67b, ROM, microcode,
периферия и MMU-less профиль не изменены. На момент выпуска пакет CP77
на плату не устанавливался. [Архивный пакет RT-11](../demos/rt11/service/cp77/README.md).

## UART и пульт

| Команда UART, при восьмеричном вводе | Действие |
|---|---|
| `F` | Все AC0–AC5, FPS, FEC, FEA; на панели остаётся AC0 |
| `F 0` … `F 5` | Один AC: четыре 16-битных слова, старшее первым |
| `F 6` | FPS и выбранные режимы F/D, I/L |
| `F 7` | FEC |
| `F 10` | FEA, адрес команды с FP-ошибкой |
| `D [addr [count]]` | Integer/FIS/FP11 disassembly |

Это **просмотр без изменения состояния FP**. Сами FP-инструкции монитор
не исполняет. `F 0 123` отвергается целиком; команд записи AC/FPS пока нет.
Селекторы числовые: после `X` FEA выбирается `F 8`, после `O` — `F 10`.
Значения AC выводятся без потерь, в восьмеричном/HEX представлении. Это
сырые четыре слова D-регистра, включая сохранённую младшую половину в F-mode;
десятичное форматирование floating-point не добавлено.

В меню **MEM → FP REGISTERS → ENTER** открывается AC0. PREV/NEXT выбирают
одну из девяти строк, переход закольцован; ENTER переходит к следующей,
не открывая редактор. Выбранный регистр остаётся до нового нажатия.
Если строка длиннее 16 символов, прежний автоматический scroll проходит
до конца и обратно, с паузами на концах. Команда `A 0` отключает scroll.
UART `F` сразу печатает весь набор; это не перелистывает экран девять раз.
Действия панели дублируются строками `[PANEL] F ...` в UART. Кнопки 8/9
по-прежнему STEP/OVER, A — CPU-регистры; другие горячие клавиши сохранены.

Например:

```text
AC2=040200 000001 000002 000003
FPS=000200 MODE=D,I
FEC=000014
FEA=001102
```

`?FP11 DEBUG STATE UNAVAILABLE` означает отсутствие активного модуля,
неподдерживаемый debug descriptor или ошибку его чтения. Старые версии FP
до CP77 не предоставляют descriptor; обычный просмотр памяти/GPR работает.

## Дизассемблер

Добавлены пять controls, LDFPS/STFPS/STST, float unary, арифметика,
MOD, все преобразования и различие CPU R0–R7 / FP AC0–AC5.
AC в двухоперандном поле ограничен AC0–AC3; mode=0 floating operand
допускает AC0–AC5. Кодировки AC6/AC7 и reserved controls выводятся `.WORD`.

Для **адреса остановленного PC** мнемоника выбирается по живому FPS:
например, `ADDD`, `STCDL`, `LDCLF`. Для любого другого адреса, а также без
доступного FP descriptor, используются общие обозначения DEC: `ADDf`,
`STCfi`, `LDCif`, `STCff`, `LDCff`. Суффикс `f` означает F/D по FPS,
`i` — I/L, `ff` — смену точности. Это намеренно не предположение о FPS
в другой точке программы. Дизассемблер не исполняет SETD/LDFPS при просмотре
и не пытается предсказать ветвления. После STEP следующая команда у PC
получает актуальную точность.

Читаются только opcode и extension words, без обращения по effective
address. Даже D/L immediate у FP11 занимает одно extension word; это
старшая часть значения. Displacement, absolute и PC-relative адреса
используют прежние правила. NEXT пропускает extension, PREV возвращается
по истории фактически просмотренных границ. При недоступном extension
остаётся `.WORD` с отметкой `[EXTENSION UNAVAILABLE]`.

Кодировки сверены с локальным DEC FP11-A User's Manual, гл. 5,
и [DEC Architecture Handbook 1983](https://www.bitsavers.org/pdf/dec/pdp11/handbooks/EB-23657-18_PDP-11_Architecture_Handbook_1983.pdf).

## Размещение и descriptor

Прежнему ODT оставалось 428 байт до `040000`. В CP77 его разрешённая
область увеличена до `060000`, а FP11 перенесён на `060000`.

| Область | Восьмеричные адреса | Байты |
|---|---|---:|
| ODT immutable code/data | 010000–040465 | 12598 |
| ODT BSS/stack | 040466–042543 | 1070 |
| Весь ODT | 010000–042543 | 13668 |
| Свободно до FP11 | 042544–057777 | 6812 |
| FP11 immutable code/descriptor | 060000–070303 | 4292 |
| FP11 BSS/stack | 070304–070717 | 268 |
| Весь FP11 | 060000–070717 | 4560 |
| Свободно после FP11 до I/O | 070720–157777 | 28208 |

ODT вырос на 1808 байт относительно CP67, FP11 — на 10 байт относительно
CP76 (только descriptor). Текст реализации FP начиная с `COLD:` побайтно
совпадает с CP76; абсолютные адреса машинного кода пересобраны DEC MACRO/LINK.
Численная модель, addressing modes, exceptions и FP stack прежние.

В начале FP-модуля находятся `JMP @#COLD` (060000), сигнатура `125110,2`
(060004), затем debug descriptor:

| Адрес | Содержимое |
|---|---|
| 060010 | magic `125104` |
| 060012 | version `1` |
| 060014 | абсолютный адрес FPS, сейчас 070304 |
| 060016 | абсолютный адрес AC0, сейчас 070312 |
| 060020 | MEMEND, сейчас 070720 |

FEC=FPS+2, FEA=FPS+4, AC0=FPS+6; каждый AC занимает 8 байт.
ODT проверяет service-ready FP, обе сигнатуры/версии, чётность и границы
FPS, положение FP entry vector и размещение всех шести AC до MEMEND/I/O.
Чтение descriptor защищено прежним fault recovery монитора. Адреса состояния
не вшиты в ODT: изменение длины FP-кода не требует нового ODT при сохранении
descriptor ABI и базы модуля. Сама база 060000 — фиксированный договор этой
версии. Это не relocation и не автоматический FRAM allocator.

Общий формат файла и таблицы модулей остаётся CP67 ABI3 / format2.
Cold initializer очищает BSS и публикует FP-ready последним. ROM, EBR и
механизм самоинициализации не менялись. Порядок загрузки/обновления при
наличии старого FP описан в пакете; не смешивать ODT CP77 с размещением FP
CP76 на 040000.

## Проверки и воспроизведение

```sh
python3 tools/build_odt_cp77.py
python3 tools/test_odt_cp64.py --odt build/cp77-odt --out build/cp77-odt-core
python3 tools/test_odt_break_cp77.py --odt build/cp77-odt --out build/cp77-breakpoints
python3 tools/test_odt_fp_cp77.py
python3 tools/test_odt_panel_cp77.py --odt build/cp77-odt --out build/cp77-panel
python3 tools/test_fp_paths_cp77.py --mode logic
python3 tools/test_fp_events_cp77.py
python3 tools/test_fp_events_cp77.py --vendor
python3 tools/run_fp11_rt11_cp77.py --out build/cp77-rt11-new
python3 tools/prepare_dec_fp_cp77.py
```

Исторические CP64–76 воспроизводятся из их source archives: рабочие исходники
уже CP77. Hardware tests используют прежние замороженные модели, UART/panel
регрессии — существующий CPU harness, финальная RT-11 проверка — полный
CP67b board с SPI FRAM/SD. Точные результаты сохраняются отдельно в архиве CP77.

## Внешние DEC-диагностики: результат и граница совместимости

Из локального `lsi11/disks/xxdp25.dsk` извлечены **FFPAA1.BIN,
FFPBA0.BIN, FFPCB0.BIC**. Их назначение и порядок соответствуют DEC
FP11-A User's Manual §7.2. Это именно части DFFPA/B/C; похожее имя
ZFPAA0 относится к Command Bridge/FEP и для FP11 не использовалось.

Извлечение проверяет длины цепочек 60/59/59 блоков, отсутствие циклов,
все 74/59/59 absolute-loader record checksums и конечную запись.
Исходный диск читается без записи; его SHA256 сохранён. BIN/BIC,
полученные memory images и manifest позволяют воспроизвести запуск.
Загрузка XXDP и запуск DFFPA также проверены в SIMH 3.12-3, PDP-11/70,
с диском read-only: диагностика выдаёт проходы без ошибок. Это baseline
диагностического образа, а не проверка платы uJ11.

**Без дополнений к шине uJ11 DFFPA не проходит.** В тесте 2 по `004706`
исполняется `MOV @#177776,R3`, после CFCC. Memory-mapped PSW в CP67b
отсутствует: trap 4 сохраняет PC `004712`; диагностика сообщает одну
ошибку. Журнал не удалён и не помечен PASS. Поддержка этого архитектурного
регистра — отдельный следующий hardware/microcode checkpoint, со своим
synthesis gate; текущий RTL ради диагностики не менялся.

Для изоляции FP-семантики есть явный `--psw-read-adapter`: **только в
тестовой модели шины** чтение `177776` возвращает настоящий `dut.psw`.
Запись PSW не эмулируется. Диагностические программы не патчатся;
математика, flags, traps и FP state исполняются обычным HALT firmware
на неизменённом CPU. Остальная модель: RAM без wait, KL11 TX-ready/
RX-empty, console switches=0, прочие внешние I/O адреса дают bus error.

| Оригинальная диагностика, с test-only PSW read | Проходы | Ошибки | Core clocks |
|---|---:|---:|---:|
| FFPAA1 / DFFPA Part 1 | 1 | 0 | 177182640 |
| FFPBA0 / DFFPB Part 2 | 1 | 0 | 2669489 |
| FFPCB0 / DFFPC Part 3 | 1 | 0 | 1476999 |

Это успешные **FP-диагностики в указанном стенде**, не полная совместимость
11/34 и не native PASS CP67b. Manufacturing abort checks DFFPB с модулем
M8267-TA не применяются: этого оборудования нет. Проверки IRQ/ODT/operand
faults по контракту uJ11 остаются отдельными directed tests. Counts выше
не являются временем исполнения на SPI FRAM и не входят в board benchmarks.

```sh
# Ожидаемая ошибка test 2 из-за отсутствующего memory-mapped PSW:
python3 tools/run_dec_fp_cp77.py FFPAA1.BIN
# Три проверки только FP-семантики, с явно указанным ограничением стенда:
python3 tools/run_dec_fp_cp77.py FFPAA1.BIN --psw-read-adapter
python3 tools/run_dec_fp_cp77.py FFPBA0.BIN --psw-read-adapter
python3 tools/run_dec_fp_cp77.py FFPCB0.BIC --psw-read-adapter
```

## Итог CP77

| Проверка | Результат |
|---|---|
| Прежние memory/GPR/integer-disassembly команды | 187 checks / 175 commands / 5493498 clocks, PASS |
| Breakpoints, IRQ/trace, STEP OVER | 112 checks / 103 commands / 4026967 clocks, PASS |
| FP debugger UART, весь диапазон opcode | 9262 checks / 5133 commands / 209981546 clocks, PASS |
| Пульт, HDSP и scroll | 3324 checks / 116 окон / 139930342 clocks, PASS |
| Перенесённый FP11, logic decode | 215784 cases / 15052576 checks / 6014 manual, PASS |
| FP faults/IRQ/ODT, sync и vendor DP8KC | По 111 cases / 4174 checks, PASS |
| Полная RT-11/FRAM/SD/UJMOD/43 STEP | 99 checks / 832912588 clocks / 9776 UART bytes, PASS |

Все 215784 FP input/expected rows побайтно совпали с CP76 logic; повторная
сверка численного эталона не подменяет собой выполнение перенесённого кода.
ODT FP-тест покрывает все 4096 слов 170000–177777, 1008 сочетаний режима FPS,
invalid AC, длины команд, F/I register forms, raw state, синтаксические ошибки,
повреждение descriptor и bus error его чтения. Прежний тест `.WORD 177777`
обновлён: это допустимая кодировка LDCff с deferred PC-relative operand.

Во время подготовки harness исправлены устаревшее имя баннера CP66,
ссылка на образ CP76 в directed runner и ожидаемая длина строки AC в тесте
scroll. Реализация ODT/FP не менялась по результатам этих исправлений.
Внешняя ошибка DFFPA без PSW read сохранена отдельно как compatibility gap.

Все 53 аппаратных входа CP67b совпали по SHA256. Нового synthesis нет;
прирост FPGA **0 LUT / 0 FF / 0 EBR / 0 uwords**. Сохраняются 1244 LUT,
381 FF, 7 EBR, 1005/1024 uwords и TRACE 32,032 MHz. Физическая плата
по-прежнему имеет CP67b и прежние ODT/SDBOOT; установка пакета CP77 не выполнялась.

[Архив исходников и журналов](../tb/reports/cp77/archive.json).
Проверить архив и текущие исходники: `python3 tools/verify_cp77.py --current`.
