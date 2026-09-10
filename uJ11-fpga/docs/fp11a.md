# CP30: начало FP11(A), управляющие команды и постоянное состояние

> Исторический эксперимент CP30. По решению пользователя от 2026-09-10
> FP11 отложен; в CP31 его RTL, decoder, state и микрокод удалены из рабочей
> сборки. Для воспроизведения использовать коммит `d59f19c` или snapshots
> `synth/reports/cp30*/source.tgz`. Текущая floating-point ISA — FIS.


2026-09-10. **Это первый checkpoint, не полный FP11.** ODT отложен по решению
пользователя и записан в [TODO](../TODO.md). На физической плате остаётся CP29.
FP-подмножество включается явно: `ROM_DECODE=1, FP11_CONTROL=1` у core,
`FP11_CONTROL=1` у board; default равен нулю. MMU отсутствует.

## Первоисточники и профиль

Основной источник — DEC [FP11-A Floating-Point Processor User's Guide,
EK-FP11A-UG-001](https://ftpmirror.your.org/pub/misc/bitsavers/www.computer.museum.uq.edu.au/pdf/EK-FP11A-UG-001%20FP11-A%20Floating%20Point%20User%27s%20Manual.pdf),
§4.4, таблица 4-1, §5.1, таблица 5-2 и §5.3.15–5.3.22. URL и SHA256
скачанного PDF сохранены в [manifest](verification-cp30.json).
Цель — программная FP11-совместимость F/D; внутренние такты и 64-битный
Am2901 datapath физического FP11-A не воспроизводятся.

FP11 имеет шесть 64-битных аккумуляторов AC0–AC5 и отдельные FPS/FEC/FEA.
Это отличается от существующего FIS с операндами на стеке памяти.
FPS использует биты 15,14,11…5,3…0; маска хранимых полей — `CFEF` hex.
Переключение F/D меняет бит 7, I/L — бит 6. `CFCC` переносит только четыре
FP condition codes в PSW; остальные проверенные команды сохраняют PSW.

Для **только реализованного управляющего подмножества** вторым независимым
эталоном служит существующий [`core/pdp11_fp.c`](../../core/pdp11_fp.c),
DCJ11, `ENABLE_MMU=0`. Он использует integer FP representation; host float
в проверке этих команд не участвует. Исходники core и `microasm11` не менялись.
Поведение будущих arithmetic exceptions, FEA, operand abort/restart будет
проверяться отдельно по DEC; профиль C-эмулятора нельзя автоматически назвать
точным FP11-A во всех этих случаях.

Ранее исследованные AM4 и microcpu содержат **FIS**, а не готовую реализацию
полного FP11. Их успешный fit не является доказательством fit FP11(A).

## Что выполнено

| Команда | Encoding (octal) | Реализованный операнд |
|---|---|---|
| CFCC | 170000 | без операнда |
| SETF / SETI | 170001 / 170002 | без операнда |
| SETD / SETL | 170011 / 170012 | без операнда |
| LDFPS | 170100…170107 | R0…R7 |
| STFPS | 170200…170207 | R0…R7 |

Всего 21 opcode encoding, семь мнемоник. Это **настоящие микропрограммы uJ11**
в [fp11_control.uasm](../microcode/fp11_control.uasm), не firmware PDP-11.
При выключенной опции все FP encodings сохраняют прежний reserved trap.
При включённой опции остальные FP encodings пока тоже дают прежний reserved
trap; FEC=2 / FP exception 0244 ещё не реализованы. Полного FPU не заявляем,
board MAINT/FPA identification не меняли.

`STFPS R7` действительно меняет PC. `LDFPS R7` получает уже увеличенный при
FETCH PC. Режимы памяти для LDFPS/STFPS, STST, floating loads/stores,
арифметика F/D и преобразования пока отсутствуют. Наличие хранения AC0–AC5
не означает, что инструкции обращения к ним уже реализованы.

## Хранение без расширения быстрого RF

Используется свободный участок **существующего EBR opcode decoder**:
физические 9-битные ячейки `280…2BF` hex. Все 65536 opcodes проверяются
генератором на отсутствие пересечения с этим участком. Два порта 1024×9
внутри одного DP8KC одновременно читают/пишут младший и старший байты.
Во время обычного FETCH порт A выдаёт 9-битный адрес микропрограммы;
внутреннее обращение к FP-состоянию использует оба порта между FETCH.

| Внутренний word index | Назначение |
|---|---|
| 0 | FPS; при записи маска CFEF |
| 1 / 2 | место для FEC / FEA |
| 3 | временное слово |
| 4…27 | место для шести AC, по четыре 16-битных слова |
| 28…31 | временные слова |

Всего 32×16 бит. Это внутренние индексы состояния, **не адреса PDP-11**.
Обычная программа не получает нового CSR или окна памяти. Внутренние
READ/WRITE не выходят на внешнюю шину, не обращаются к FRAM и не занимают
её контроллер. Основные RF16×16, Q и ALU16 сохранены; новых FP arithmetic
register FF, умножителя, делителя или barrel shifter нет.

## Микрокоманда v13

Ширина 36 бит и 1024 слова сохранены. `READ/WRITE, private=1, prefetch=0`
используют ранее свободный control bit 1. Поля A/B содержат обычные RF
selectors; A[5:1] выбирает внутреннее слово, B — данные записи. Обращение
должно быть словным и выровненным; `byte`, `stream`, `fault_inc` и prefetch
несовместимы с private access и отвергаются assembler.

`JUMP, target=FETCH, fp_init=1` использует тот же bit 1 в контексте JUMP.
Это последний шаг reset microprogram, после очистки RF: фиксированные
A=B=R0 уже дают нулевые адрес и данные, и за тот же такт очищается FPS.
Сам `RESET` opcode не является сбросом CPU и не вызывает этот hook.
AC/FEC/FEA после reset не обещаются очищенными. Полная инициализация не
требует ещё одного счётчика или дерева reset/mux на 16 бит данных.

Из прежних 954 занятых слов изменился только bit 1 reset JUMP по uaddress
`010` hex; **953 слова и все старые метки сохранены точно**. Новые routines
заняли 33 прежние свободные ячейки. Итого **987/1024×36**, свободно 37 слов.
Проверка микрокода также доказывает, что control bit 1 используется только
в JUMP/READ/WRITE; это позволяет декодировать FP read/write компактно.

## Реальные HC1200 checkpoints

Все результаты относятся к **полной плате** с integer/EIS/FIS, FRAM,
KL11/KW11, panel/HG GPIO, SD/RK, bootstrap, OSCH/reset и физическими pins.
Prefetch выключен. Diamond/Synplify, LCMXO2-1200HC-4SG32C, 29.56 MHz.

| Revision | FP control | LUT4 | FF | EBR | Slices | TRACE MHz | Результат |
|---|---|---:|---:|---:|---:|---:|---|
| CP29a, прошитый baseline | нет | 1239 | 326 | 6 | 621 | 30.609 | PASS |
| [CP30a](../synth/reports/cp30a/result.json) | да | 1329 | 327 | 6 | 668 | — | MAP: превышена площадь |
| [CP30b](../synth/reports/cp30b/result.json) | да | 1275 | 327 | 6 | 643 | — | MAP: превышены slices |
| [CP30c](../synth/reports/cp30c/result.json) | да | 1281 | 327 | 6 | 644 | — | MAP: превышена площадь |
| [CP30d](../synth/reports/cp30d/result.json) | да | **1265** | **327** | **6** | **635** | **31.284** | **PASS** |
| [CP30e](../synth/reports/cp30e/result.json), тот же RTL | нет | **1230** | **326** | **6** | **618** | **30.116** | **PASS** |

Первый вариант остановлен до расширения арифметики. CP30b убрал отдельный
reset mux данных/адреса FP-памяти и упростил opcode index. CP30c попробовал
компактный FP-tag; локальное упрощение само по себе не улучшило общий MAP.
CP30d заменил приоритетную цепочку чтения периферии параллельными масками
взаимоисключающих устройств. Разница относительно CP30e: **+35 LUT, +1 FF,
+0 EBR, +17 slices**. Это разница двух полных mapped designs, не изолированная
арифметическая стоимость каждого блока. В обоих успешных вариантах все
соединения разведены, TRACE constraint проходит.

У CP30d остаются **15 LUT / 5 slices / 1 EBR**. Желаемые <=1100 LUT и 50 MHz
не достигнуты. Fit арифметики FP11 не доказан. Перед расширением нужно решить
площадь и размещение микрокода: 37 свободных слов не являются бюджетом,
достаточным для обещания полного FP11. External pin delays и худший допуск
OSCH не закрыты; текущий Fmax — внутренний TRACE, не новое измерение платы.

В каждом каталоге synthesis сохранены inputs, raw reports и `source.tgz`,
включая неудачные варианты. После CP30d/e исправлен только номер версии в
assembler statistics с 12 на 13; SHA256 ROM остался тем же. HDL и vendor ROM
побайтно совпадают со снимками synthesis. Сравнение сохранено в manifest.

## Проверки и измерения

- 90 112 differential cases на portable ROM и неизменённых DP8KC/GSR/PUR:
  все 65 536 входных значений LDFPS, 20 480 случаев управляющих команд,
  4096 случаев R0…R7, 0…3 внешних wait states. Сравниваются все регистры,
  FPS, PSW и отсутствие внешних data transfers. FPS устанавливается и
  считывается настоящими LDFPS/STFPS, без подмены FP RAM тестбенчем.
- 448 дополнительных случаев на обеих ROM-моделях: повторный reset очищает
  записанный FPS, все NZVC, семь команд, trace/IRQ и их одновременность;
  проверены frame PC/PSW и преимущество trace перед IRQ.
- 66 720 проверок общей памяти на каждой модели: все 32 слова, walking
  ones/zeros, byte packing, disabled hold, все 65536 dispatch entries после
  записи FP-состояния. При выключенном FP — прежняя exhaustive decode parity.
- 16 777 216 комбинаций адресов и overlay/RK/direction states доказали
  непересечение селекторов и совпадение нового read mux со старым для
  выбранных устройств. Невыбранный/неподтверждённый read теперь возвращает 0;
  bus timeout/error и ACK policy не менялись.
- 12 928 integer core cases и strict core lint; 24 Python tests;
  23 840 FIS cases с synchronous decoder, включая 3072 memory faults;
  с **FP11_CONTROL=1** тот же corpus отдельно прошёл RAM и SPI FRAM без prefetch.
  RAM CSV (результаты и все счётчики) с FP выключенным/включённым совпали побайтно.
  Board units включают portable/vendor decoder и firmware ROM, оба FRAM
  divider, IRQ assist, KW11 и 29 реальных peripheral beats.
- Полная холодная RT-11 + DIR в симуляции с **FP11_CONTROL=1**:
  **355 134 893 clocks**, 3 983 747 retirements, 3270 UART wire bytes,
  162 SD reads / 6 writes. Образ диска не изменён. Это совместимость текущего
  подмножества с boot workload, а не проверка FP arithmetic средствами RT-11.

Счётчики первого instruction-level corpus: 757760 clocks, максимум 13
на инструкцию при 0…3 wait states. При нулевых ожиданиях и sync decoder:

| Команда | Microclocks, включая opcode fetch | Внешние memory beats | Внутренние FP state cycles |
|---|---:|---:|---|
| LDFPS Rn | 6 | 1 | 1 write |
| STFPS Rn | 6 | 1 | 1 read |
| CFCC | 10 | 1 | 1 read |
| SETF / SETI / SETD / SETL | 10 | 1 | 1 read + 1 write |

Это CPI на тестовой RAM; opcode из SPI FRAM добавляет её реальную задержку.
Сервисные SD/RK программы и устройство памяти не входят в эти ideal-RAM CPI.

Воспроизведение (существующий assembler `microasm11` не изменять):

```sh
make microcode-m0 board-decode
make test-fp-control vendor-fp-control
make test-python test-core test-board-units test-board-selectors
python3 tools/check_sync_decode.py --suite fis
python3 tools/check_fp_compat.py --mode -1
python3 tools/check_fp_compat.py --mode 1
python3 tools/benchmark_fp_control.py
python3 tools/run_board.py --tag cp30-check --fp11-control
# На Linux/Diamond, в новом снимке и с новым именем:
python3 tools/checkpoint_board.py cp30f --fp11-control
```

Vendor models должны быть в `build/vendor`. Raw logs, oracle vectors,
source hashes и counts сохранены в [verification manifest](verification-cp30.json).
FP11 в стандартном board top остаётся выключенным, пока не готова ISA.
Плата и её FRAM/SD в CP30 не программировались и не изменялись.

## Следующая граница

1. Сокращение/переразмещение control store с сохранением integer/FIS tests;
   отдельный измеренный бюджет полного FP11 перед арифметикой.
2. LDFPS/STFPS addressing modes (с отдельным word/float qualifier вместо
   текущего integer byte_instruction), STST/FEC/FEA и ошибки FP opcode/mode;
   затем AC load/store/clear/test/abs/neg в F/D.
3. F/D arithmetic и преобразования через ALU16/Q/микроциклы, rounding и
   truncate, dirty zero/undefined variable, точные exception/abort tests.

ODT остаётся в TODO. MMU и резервирование EBR под него не добавляются.
