# CP69: адресация управляющих команд FP11 и STST

2026-09-14. Программный модуль FP11 в HALT FRAM расширен до **8 мнемоник /
197 кодировок**. LDFPS, STFPS и STST поддерживают все восемь режимов адресации.
CFCC, SETF/SETD и SETI/SETL сохранены. Это всё ещё часть эмулятора:
**LDF/STF, арифметики и преобразований нет**. Неподдержанные `17xxxx`
формируют FEC=2, FER=1, FEA=адрес opcode с учётом FID, как в CP68.

FPGA остаётся CP67b без MMU, FIS исполняется прежним микрокодом.
Новый FP11.BIN проверен в симуляции; **на физическую плату не установлен**.
[Готовый пакет и установка](../demos/rt11/service/cp69/README.md).

## Документация и профиль совместимости

Первичный источник — DEC **EK-FP11A-UG-001**, May 1978:
таблица 5-2, печатная 5-9; §5.3.15 LDFPS, §5.3.16 STFPS,
§5.3.17 STST, печатная 5-20; FPS в §4.4.
[Скан DEC](https://www.bitsavers.org/www.computer.museum.uq.edu.au/pdf/EK-FP11A-UG-001%20FP11-A%20Floating%20Point%20User%27s%20Manual.pdf),
SHA256 `be6446903afc3d6ca0e1b0efed85301853979e3a1c5631c8a6e707165c63ecf6`.

STST имеет фиксированный размер: два слова в памяти, одно слово при
register direct и immediate, независимо от FD/FL. В prose §5.3.17 встречается
слово «accumulator» для mode 0; таблица 5-2 и integer-формат F4 указывают
CPU register. Здесь mode 0 использует **R0–R7**, что также соответствует
существующему DCJ11 oracle `core/pdp11_fp.c`.

Порядок изменения R0–R6 при ошибках берётся из существующего DCJ11 oracle:
`GeteaFW/FP` и `fp_reg_change`. Это явно выбранный профиль CPU, а не утверждение,
что руководство FP11-A отдельно описывает все J-11 bus abort corner cases.
Исходники oracle и их hashes сохранены в архиве CP69. Общий `core/`
не изменён; тест отключает fast RAM и наблюдает реальные read/write callbacks.

## Команды и effective address

Все opcode, значения FPS и адреса ниже восьмеричные. LDFPS `170100–170177`
загружает FPS с маской `147757`. STFPS `170200–170277` сохраняет FPS.
STST `170300–170377` сохраняет сначала FEC, затем FEA, если результат
двухсловный. Эти команды сохраняют весь CPU PSW, включая NZVC, и не меняют
AC0–AC5. У STST также сохраняются FPS/FEC/FEA. CFCC `170000` переносит только
NZVC из FPS в CPU; SETF `170001`, SETI `170002`, SETD `170011`, SETL `170012`
меняют соответствующие FD/FL bits.

| Mode | Адресация | LDFPS/STFPS | STST |
|---|---|---|---|
| 0 | Rn | Одно слово CPU register | Только FEC в CPU register |
| 1 | (Rn) | Одно слово | FEC и FEA |
| 2 | (Rn)+ | Rn += 2 | R0–R6 += 4; R7 += 2, только FEC |
| 3 | @(Rn)+ | Сначала pointer word, Rn += 2 | То же, затем два слова результата |
| 4 | -(Rn) | Rn -= 2 | Rn -= 4, включая R6/R7 |
| 5 | @-(Rn) | Rn -= 2, затем pointer word | То же, затем два слова результата |
| 6 | X(Rn) | Extension word и одно слово операнда | Extension word и два слова результата |
| 7 | @X(Rn) | Extension, pointer, operand | Extension, pointer, два слова результата |

Mode 2/R7 — immediate: LDFPS читает следующее слово потока, STFPS/STST
записывают это слово и продолжают за ним; STST в этом случае пишет только FEC.
Mode 3/R7 — absolute. При indexed R7 база PC берётся **после** чтения displacement.
При mode 0/R7 LDFPS получает PC после opcode, STFPS/STST меняют PC продолжения.
Обновления 16-bit адресов выполняются с переполнением по модулю 65536.

R0–R6 autoincrement/decrement фиксируется только после всех успешных operand
accesses. Для STST ошибка второй записи оставляет первую запись FEC видимой,
но не фиксирует pending autoupdate. Изменения PC для modes 2–5 выполняются
до доступа; PC после indexed extension обновляется только при успешном её
чтении. При последующей ошибке этот уже прочитанный extension не отменяется.
Нечётный word address вызывает USER bus/address trap 004. Для `@-(PC)`
opcode сам становится pointer; младший бит этих трёх кодировок равен 1,
поэтому получается нечётный адрес — это проверенный fault, а не успешный EA.

HALT handler временно устанавливает свой fault vector и восстанавливает
прежние 004/006 перед START. Успешная команда не использует USER stack,
кроме явно запрошенной адресации через SP. Trap строит USER frame PSW/PC;
повторная ошибка при его построении терминальна. IRQ, trace и запрос ODT
обслуживаются после восстановления USER context. Код самого FP opcode
перечитывается из USER[CPC−2]; как в CP68, нужен стабильный RAM instruction
stream. Исполнение opcode из volatile I/O не поддерживается. Обращения
операндов идут через имеющуюся карту USER памяти/периферии, новой MMU или
эмуляции внутренних J-11 I/O-регистров здесь нет.

## Размещение и сборка

Исходник `firmware/fp11/FP11.MAC`, версия `00.02`, собирается настоящими
DEC MACRO/LINK в RT-11 под SIMH. `microasm11` не используется. Модуль
**абсолютный**, формат CP67 version 2 / ABI 3; relocation не добавлена.

| Область | Адреса / размер |
|---|---|
| INIT и immutable code | 040000–041335, **734 байта / 367 PDP-11 слов** |
| FPS / FEC / FEA | 041336 / 041340 / 041342 |
| AC0–AC5, по четыре слова | 041344–041423, 48 байт |
| Saved R0–R7, PSW | 041424–041445, 18 байт |
| Fault vector и scratch, включая pending update | 041446–041465 |
| Private stack | 041466–041665, 128 байт |
| Полная аллокация | 040000–041665, **950 байт** |
| Свободный непрерывный остаток до I/O page | 041666–157777, **40 010 байт** |

FP11.BIN занимает **1536 байт / три RT-11 блока**, включая 512-byte header
и padding. Checksum — сумма 367 слов modulo 65536: `151140` (53856 decimal).
Все 216 байт mutable state находятся вне checksum, очищаются с readback
при каждом cold init. В таблице модулей записана длина immutable code;
весь диапазон до MEMEND нужно резервировать отдельно. Автоматического BSS
allocator нет. От CP68 добавлено 278 байт кода и 4 байта состояния.

INIT проверяет CP67 marker/UJSTAT, очищает данные, устанавливает HALT vector
010/012 и публикует FP-ready последним, сохраняя ODT/debug-ready.
Векторы и модульный ABI описаны в [CP68](fp11-firmware-cp68.md) и
[CP67](retained-modules-cp67.md). BSS symbols меняются между версиями;
эти адреса не являются ABI будущего полного эмулятора.

## Проверки

| Модель / сценарий | Результат |
|---|---|
| DCJ11 differential, sync ROM | **5012 cases / 348576 checks — PASS** |
| Logic decode | **665 cases / 47540 checks — PASS** |
| Vendor DP8KC | **665 cases / 47540 checks — PASS** |
| Directed faults/IRQ/T/ODT, sync и vendor | **28 cases / 1008 checks** каждый — PASS |
| Полная SPI FRAM модель, ODT/FP/SDBOOT | **4 scenarios / 366 checks — PASS** |
| RT-11/UJMOD/ODT, SPI SD/FRAM, UART waveform | **33 checks / 503618729 clocks / 2921 UART bytes — PASS** |

Differential набор перебирает все 192 transfer encodings с 16 начальными
состояниями, отдельно пять special commands, затем ошибки каждого наблюдаемого
operand access. Включены **132 нечётных адреса**, ошибки pointer/extension,
обе записи STST, R6/R7, immediate/absolute/PC-relative, задержки ACK 0–3 clocks.
Сравниваются FPS/FEC/FEA, полный PSW, R0–R7, все 24 слова AC, восстановленный
fault vector и **весь журнал USER-записей**, включая stack frame при trap.
Logic/vendor subset сохраняет каждую кодировку, все injected faults и
все odd-address probes. Это не полный перебор 65536 адресов и состояний FPS;
исчерпывающая проверка маски LDFPS остаётся в историческом архиве CP68.

Directed regression сохраняет проверки unsupported/FID, opcode reread fault,
double fault, нечётного SP, IRQ, RTT/T, trace priority и ODT request внутри FP.
SPI сценарии проверяют оба порядка cold init ODT/FP, повторный reset после
загрязнения BSS и отказ FP checksum с сохранением ODT.

Полный RT-11 тест устанавливает три модуля настоящим UJMOD. Через UART ODT
пошагово исполняются SETD, SETL, STFPS с PC-relative адресом, подготовка R2,
LDFPS immediate, inhibited unsupported opcode, STST (R2)+ и STFPS R1.
FPTST самостоятельно проверяет результаты, возвращается в RT-11, выполняется
DIR. Следующий cold reset очищает FP state без повторной установки, OFF2 и
reset отключают только FP. Номер 2 относится к этой тестовой таблице.
Окно ESC сокращено двумя константами только в тестовом firmware ROM.
Это симуляция, не замена электрической проверки на физической плате.

Воспроизведение из `uJ11-fpga`, последовательно:

```sh
python3 tools/test_fp_memory_cp69.py
python3 tools/test_fp_memory_cp69.py --mode logic
python3 tools/test_fp_memory_cp69.py --mode vendor
python3 tools/test_fp_events_cp69.py
python3 tools/test_fp_events_cp69.py --vendor
python3 tools/test_fp_board_cp69.py
python3 tools/run_fp11_rt11_cp69.py --out build/cp69-fp11/rt11-new
python3 tools/verify_fp69.py
```

Нужны Verilator, Icarus и Lattice vendor models, C compiler, SIMH `pdp11`,
штатный `lsi11/rt11tool`, RT-11 образ `lsi11-fpga/images/rt11v503.dsk`.
Работа ведётся с частными копиями дисков. RT-11 `--out` должен быть новым
каталогом. Архив содержит raw logs, CSV, native LST/MAP/OBJ/SAV и исходники
с SHA256; `verify_fp69.py --current` дополнительно проверяет текущие inputs.
[Неизменяемый архив CP69](../tb/reports/cp69/archive.json).

## Скорость и ресурсы

Результаты полной SPI FRAM модели; интервал от первого USER opcode request
до окончания START с восстановленным USER context. Bus beats — CPU memory
transactions. Для строк с memory modes используется R2, displacement `02000`.

| Команда / mode | SPI clocks | Bus beats |
|---|---:|---:|
| CFCC / SETF / SETI / SETD / SETL | 8590 / 8208 / 8400 / 8592 / 8784 | 135 / 129 / 132 / 135 / 138 |
| LDFPS Rn / STFPS Rn / STST R0 | 11375 / 11513 / 12531 | 179 / 181 / 197 |
| (Rn): LDFPS / STFPS / STST | 11733 / 12062 / 13524 | 185 / 190 / 213 |
| (Rn)+ | 13180 / 13509 / 14971 | 208 / 213 / 236 |
| @(Rn)+ | 13427 / 13756 / 15218 | 212 / 217 / 240 |
| -(Rn) | 13180 / 13509 / 14971 | 208 / 213 / 236 |
| @-(Rn) | 13427 / 13756 / 15218 | 212 / 217 / 240 |
| X(Rn) | 12614 / 12943 / 14405 | 199 / 204 / 227 |
| @X(Rn) | 12790 / 13119 / 14581 | 202 / 207 / 230 |

Вход до первого запроса handler — **316 clocks**, START fetch→USER return —
**177 clocks**, как в CP68. При 29,56 MHz STST memory занимает около
**457,51–514,82 µs**. В архиве отдельно сохранены ideal RAM и SPI CSV;
как в CP68, граница старта ideal RAM — выход bootstrap START перед fetch,
поэтому их разность не является чистой стоимостью задержек SPI.

Есть измеренная регрессия: **LDFPS Rn +1698 clocks** (9677→11375),
**STFPS Rn +1960** (9553→11513). Причина — общая подготовка mode/register/EA.
Special commands стали на 63 clocks длиннее из-за дальнего JMP RETURN.
Это зафиксированный baseline корректности; вернуть короткий register path
можно отдельной оптимизацией после реализации следующих команд. Скорость
FP-арифметики по этим числам оценивать нельзя.

Все **53 обычных файла synthesis manifest CP67b** совпали побайтно.
Нового синтеза нет: прежний измеренный результат **1244 LUT / 381 FF /
7 EBR / TRACE Fmax 32,032 MHz**, nominal 29,56 MHz, 1005/1024 microinstructions.
Прирост **0 LUT / 0 FF / 0 EBR / 0 microinstructions**; свободны прежние
36 LUT и 0 EBR. Следующий этап — LDF/STF и правила F/D для AC, затем
арифметика с отдельными FP11-A rounding/exception tests.
