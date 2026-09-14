# CP71: FP11-A CLR, TST, ABS, NEG и CMP

2026-09-14. Программный модуль HALT FRAM дополнен **CLRF/CLRD, TSTF/TSTD,
ABSF/ABSD, NEGF/NEGD и CMPF/CMPD**. Сохраняются все управляющие команды и
переносы CP70. Всего **22 мнемоники / 1189 корректных кодировок**; ещё
32 сочетания с AC6/AC7 в mode 0 явно дают illegal FP exception.
F/D-мнемоники делят opcode и выбираются FPS.FD.

ADD/SUB/MUL/DIV, преобразования и остальные FP-команды ещё не реализованы
и дают FEC=2 с учётом FID. FIS остаётся в микрокоде. Модуль проверяется на
прежнем CP67b без MMU; **на физическую плату CP71 не установлен**.
[Пакет и установка](../demos/rt11/service/cp71/README.md).

## Документация и семантика

Основной источник: DEC **EK-FP11A-UG-001**, May 1978,
§1.3, §4.4, §5.2, таблица 5-2 и §5.3.7–5.3.10, печатные 5-12/5-13.
[Скан руководства FP11-A](https://www.bitsavers.org/www.computer.museum.uq.edu.au/pdf/EK-FP11A-UG-001%20FP11-A%20Floating%20Point%20User%27s%20Manual.pdf),
SHA256 `be6446903afc3d6ca0e1b0efed85301853979e3a1c5631c8a6e707165c63ecf6`.
CMP и особый случай двух нулей дополнительно сверены с описанием CMPF/CMPD
на печатной стр. 140
[PDP-11 Architecture Handbook 1983](https://www.bitsavers.org/pdf/dec/pdp11/handbooks/EB-23657-18_PDP-11_Architecture_Handbook_1983.pdf).

| Команда | Восьмеричный opcode | Результат и FPS.NZVC |
|---|---|---|
| CLRF / CLRD | 170400 + FDST | Точный ноль в operand; N=0, Z=1, V=C=0 |
| TSTF / TSTD | 170500 + FSRC | Без записи operand; N=sign, Z=(exponent=0), V=C=0 |
| ABSF / ABSD | 170600 + FDST | Очистить sign; exponent=0 превращается в точный ноль; N=0, Z по результату, V=C=0 |
| NEGF / NEGD | 170700 + FDST | Изменить sign; exponent=0 превращается в точный ноль; N/Z по результату, V=C=0 |
| CMPF / CMPD | 173400 + AC×100 + FSRC | Сравнить **FSRC−AC**; N=(FSRC<AC), Z=равенство, V=C=0 |

Все прочие FPS-биты и CPU PSW сохраняются, кроме явно требуемой регистрации
FP exception. Это работа над raw F/D словами: host floating point,
округление, hardware multiplier/divider не используются. Представление,
порядок слов и режимы адресации описаны в [CP70](fp11-transfers-cp70.md).

Mode 0 выбирает AC0–AC5; двухбитное поле CMP AC — только AC0–AC3.
AC6/AC7 запрещены до изменения operand/FPS.NZVC. При F mode операции
изменяют лишь старшие два слова AC, сохраняя младшие два. D mode использует
все четыре слова. При CMP любой exponent=0 сравнивается как точный ноль,
независимо от sign/fraction. Если **оба operand нулевые**, CMP записывает
точный ноль в активную F/D часть destination AC; в остальных случаях AC
не меняется. Разница в последнем слове D участвует в сравнении.

CLR пропускает чтение destination, поэтому его старое значение не вызывает
undefined variable и не создаёт лишних data reads. ABS/NEG читают весь
operand в FBUF перед изменением и записью. Immediate занимает одно слово
в обоих форматах: PC+=2, недостающие слова при чтении нулевые, при записи
меняется только это слово instruction stream. Прямой autoupdate R0–R6
использует 4/8 байт, deferred — 2; правила PC и SP сохранены из CP69/CP70.

### Undefined variable: выбран именно FP11-A TST

Проверяется только operand из памяти/immediate с sign=1 и exponent=0.
При FIUV=0 ABS/NEG нормализуют его в ноль, TST ставит FN/FZ, CMP сравнивает
с нулём. AC sources не вызывают FIUV.

При FIUV=1 выставляются FER, FEC=`014`, FEA=адрес opcode. При FID=0
выполняется USER trap `244`; FID=1 подавляет trap. ABS/NEG/CMP отменяют
изменение operand и FPS.NZVC. **TST FP11-A обновляет FN/FZ и очищает FV/FC
до исключения**, как требует §5.3.8. Обновление autoincrement/decrement
фиксируется и при FP exception.

Это намеренное отличие от DCJ11: описание TST в
[J-11 User Guide, стр. 7-31](https://www.bitsavers.org/pdf/dec/pdp11/1173/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf)
задаёт undefined-variable trap до исполнения. Нельзя считать различие
FPS между двумя моделями ошибкой J-11 oracle. CP71 явно выбирает FP11-A.

### Bus fault и внешние события

Ошибка extension/pointer/data вызывает обычный USER trap `004`.
Непрочитанный полностью operand не изменяет AC/FPS. При поздней ошибке
записи CLR/ABS/NEG ранее записанные слова остаются видимыми; FPS.NZVC
фиксируется только после всех успешных записей. Pending autoupdate R0–R6
при bus fault не фиксируется, уже выполненные изменения PC сохраняются.
Это выбранный **контракт отмены программного handler**; он не заявляет
побитного совпадения всех промежуточных bus-fault состояний с физическим FP11-A.

IRQ и запрос пульта ждут завершения handler и восстановления USER context.
ODT видит целиком записанный результат ABS/NEG и финальный FPS. FP trap
приоритетнее уже ожидающего IRQ. Fault при построении trap frame терминален,
как в CP68. Opcode перечитывается из USER[CPC−2]: требуется стабильный RAM
instruction stream, volatile I/O для самого opcode не поддерживается.

## Проверки и границы oracle

Сравнение использует неизменённый `core/pdp11_fp.c`, hash
`ad5ef37ef0ee8fa9221632ad903224599759cf95a4bcbe4f5dfd77e0870f2220`.
Из **99104** строк **97176 — differential**, остальные **1928** имеют
явный marker в последнем поле вектора и отдельное ожидаемое поведение:

| Marker | Строк | Причина отдельного ожидания |
|---:|---:|---|
| 0 | 97176 | Непосредственный результат DCJ11 oracle |
| 1 | 256 | AC6/AC7: требуемый DEC FP trap подавляется ошибочным `fAbort` oracle; CLR также преждевременно меняет flags |
| 2 | 22 | Immediate `ReadFP` oracle пропускает проверку abort; handler сохраняет AC/FPS при незавершённом чтении |
| 3 | 1210 | CLR faults и ABS/NEG write faults: выбранный контракт фиксации FPS только после успеха |
| 4 | 440 | TST memory FIUV: документированное отличие FP11-A от J-11 |

Название `manual DEC cases` в строке PASS историческое: оно охватывает
также явно выбранный bus-fault контракт, а не только прямые требования DEC.
Ни одна из 1928 строк не включена в число differential cases. Общий emulator
не изменялся; его прежние два дефекта описаны отдельно в CP70.

| Проверка | Результат |
|---|---|
| Sync ROM | **99104 cases / 6947312 checks**, 1928 manual — PASS |
| Logic decode | **22748 cases / 1621848 checks**, 1654 manual — PASS |
| Vendor DP8KC | **1347 cases / 96842 checks**, 404 manual — PASS |
| Directed sync и vendor | **33 cases / 1172 checks** каждый — PASS |
| Полная SPI FRAM модель | **4 scenarios / 632 checks — PASS** |
| RT-11/UJMOD/ODT, SPI SD/FRAM, UART waveform | **43 checks / 531033015 clocks / 3476 UART bytes — PASS** |

Sync покрывает каждую корректную и illegal-AC кодировку, все modes/Rn,
F/D, FIUV/FID, AC и их сохраняемые части, dirty/negative zero, equal operands,
противоположные знаки, различие только в последнем слове D, partial writes,
**7452 injected faults и 1540 odd-address probes**. Сравниваются полный
CPU PSW, R0–R7, FPS/FEC/FEA, все 24 слова AC и USER memory write log.
ACK delays 0–3 clocks. Это не полный перебор 64-bit operands/адресов.
32-bit case ID предотвращает переполнение за 65535 строк.

Logic сохраняет все faults/odd probes, но сокращает начальные состояния.
Vendor — отдельная выборка 1347 случаев, не замена полному sync набору.
Directed suite сохраняет все предыдущие IRQ/trace/ODT/fault cases и
добавляет TSTD с FIUV/queued IRQ и ABSD с запросом ODT во время операции.

SPI suite проверяет оба порядка cold init ODT/FP, повторный RESET с грязным
BSS, отказ checksum FP с сохранением ODT. Измерены 169 операций — прежние
79 и 90 новых. Cold init обоих порядков/повтор — 3021565 clocks,
bad FP checksum — 2976154, FP с тестовой программой — 585354.
Это длительности сценариев, не отдельных FP-команд.

Интеграционный RT-11 сценарий устанавливает ODT/SDBOOT/FP через настоящий
UJMOD, делает одиннадцать UART ODT STEP, проверяет результат самой FPTST,
возвращается в RT-11, выполняет DIR, cold reset и OFF отдельного FP-модуля.
Он проверяет FP11-A TST с FIUV/FID и запись FPS/FEC/FEA в USER RAM.
ESC window укорочено двумя константами только в тестовом firmware ROM.

Из `uJ11-fpga`, последовательно:

```sh
python3 tools/test_fp_unary_cp71.py
python3 tools/test_fp_unary_cp71.py --mode logic
python3 tools/test_fp_unary_cp71.py --mode vendor
python3 tools/test_fp_events_cp71.py
python3 tools/test_fp_events_cp71.py --vendor
python3 tools/test_fp_board_cp71.py
python3 tools/run_fp11_rt11_cp71.py --out build/cp71-fp11/rt11-new
python3 tools/verify_fp71.py
```

Зависимости как в CP70: Verilator, Icarus/Lattice models, C compiler,
SIMH `pdp11`, `lsi11/rt11tool`, базовый `lsi11-fpga/images/rt11v503.dsk`.
Образ копируется в частный рабочий каталог; RT-11 требует новый `--out`.
[Архив](../tb/reports/cp71/archive.json) содержит inputs, native MACRO/LINK
assembly, вектора, логи, UART и CSV. `verify_fp71.py --current` дополнительно
проверяет соответствие текущих sources замороженным результатам.

## Размещение и установка

`FP11.MAC` версия **00.04**, DEC MACRO/LINK под RT-11 в SIMH. Абсолютный
модуль CP67 format 2 / ABI 3, без relocation; `microasm11` не используется.

| Область | Восьмеричные адреса / размер |
|---|---|
| Immutable code | 040000–042523, **1364 байта / 682 PDP-11 слова** |
| FPS / FEC / FEA | 042524 / 042526 / 042530 |
| AC0–AC5 | 042532–042611, 48 байт |
| Saved R0–R7 и PSW | 042612–042633, 18 байт |
| Fault state, pending update, ACPTR/FLEN/EADDR/FBUF | 042634–042671 |
| Private stack | 042672–043071, 128 байт |
| Полная аллокация | 040000–043071, **1594 байта** |
| Свободный непрерывный остаток до I/O page | 043072–157777, **39366 байт** |

Прирост от CP70: **342 байта кода и 2 байта BSS**, всего +344 байта.
Все 230 байт mutable state очищаются при cold init и исключены из checksum.
FP11.BIN — **2048 байт / четыре RT-11 блока**, checksum суммы 682 слов —
`057767` (24567 decimal). Таблица хранит immutable length; весь диапазон
до MEMEND=`043072` резервируется отдельно, автоматического BSS allocator нет.

Cold init проверяет BSS readback, устанавливает FP vector 010/012,
публикует ready последним и сохраняет ODT/debug-ready. Обновление активного
FP: STATUS, OFFn, cold RESET, новый FP11.BIN через UJMOD, cold RESET.
Индекс берётся из STATUS. UART ESC при cold start пропускает все модули
и выбирает встроенный bootstrap. [Пошаговая проверка FPTST](../demos/rt11/service/cp71/README.md).

## Скорость и ресурсы

Полная SPI FRAM модель прежнего CP67b. Интервал: первый USER opcode request
до завершения START с восстановленным USER context. Mode 0 — AC5, memory
modes — R2, CMP destination — AC2, immediate — одно слово. Числа в каждой
ячейке: **F / D, core clocks**. Знаки/нули исходных operands различаются
между modes; это конкретные тестовые значения, не worst-case timing.

| Operand | CLR | TST | ABS | NEG | CMP |
|---|---:|---:|---:|---:|---:|
| AC5 | 20392 / 20973 | 19837 / 20418 | 23114 / 24217 | 23306 / 24409 | 22798 / 23379 |
| (R2) | 20441 / 21394 | 18810 / 19505 | 22393 / 23982 | 22903 / 24492 | 20887 / 21582 |
| (R2)+ | 21888 / 22841 | 20257 / 20952 | 23840 / 25429 | 24350 / 25939 | 22334 / 23029 |
| @(R2)+ | 22135 / 23088 | 20504 / 21199 | 24087 / 25676 | 24597 / 26186 | 22581 / 23276 |
| -(R2) | 21888 / 22841 | 20574 / 21269 | 24721 / 26310 | 24913 / 26502 | 23535 / 24230 |
| @-(R2) | 22135 / 23088 | 20504 / 21199 | 24087 / 25676 | 24597 / 26186 | 22581 / 23276 |
| X(R2) | 21322 / 22275 | 19691 / 20386 | 23274 / 24863 | 23784 / 25373 | 21768 / 22463 |
| @X(R2) | 21498 / 22451 | 20184 / 20879 | 24331 / 25920 | 24523 / 26112 | 23145 / 23840 |
| immediate | 20942 / 21001 | 19440 / 19499 | 22576 / 22635 | 23086 / 23145 | 22319 / 23124 |

Получено **18810–26502 clocks**, около **636–897 µs** при nominal 29,56 MHz.
Вход до handler — прежние 316 clocks, START fetch→USER return — 177.
Bus beats и ideal RAM timings записаны отдельно в CSV; ideal interval
начинается с выхода bootstrap START перед opcode fetch.

Общая обработка FBUF/flags и дополнительные dispatch comparisons замедлили
прежние переносы относительно CP70. Измеренная регрессия: LDF/LDD AC5
**+4255/+4777 clocks** (теперь 21369/22472); memory LDF/LDD +2341…2400;
STF/STD AC5 +1225, memory +1652. Остальные 43 управляющих случая по clocks
не изменились, в том числе LDFPS/STFPS Rn 11755/11893. Короткие register
paths и dispatch требуют отдельной программной оптимизации после этого
функционального baseline; замедление не скрывается за неизменным Fmax.

Все **53 обычных файла** synthesis manifest CP67b совпали по SHA256.
Прирост **0 LUT / 0 FF / 0 EBR / 0 microinstructions**, нового синтеза нет.
Сохраняется измеренный baseline: **1244 LUT / 381 FF / 7 EBR / Fmax 32,032 MHz**,
1005/1024 microinstructions; свободны 36 LUT и 0 EBR. RTL, microstore и
FPGA firmware ROM прежние. Следующие функциональные этапы — ADD/SUB F/D,
затем MUL/DIV и преобразования, с отдельными rounding/exception tests.
