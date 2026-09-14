# CP70: загрузка и сохранение FP11 в форматах F/D

2026-09-14. В программный FP11-модуль добавлены **LDF/LDD и STF/STD**:
перенос между памятью и AC0–AC3, между AC0–AC5, все восемь режимов адресации,
immediate, ошибки памяти и undefined variable. Управляющие команды CP69
сохранены. Всего **693 корректные кодировки**, включая 197 прежних;
ещё 16 сочетаний с AC6/AC7 в mode 0 явно вызывают illegal FP exception.
F/D-мнемоники используют одинаковый opcode и различаются текущим FPS.FD.

Это **ещё не полный FP11**: арифметика, преобразования и остальные команды
не реализованы. Они по-прежнему формируют FEC=2 с учётом FID.
FIS остаётся в микрокоде, hardware CP67b без MMU сохранён побайтно.
Новый модуль проверен в симуляции, **на физическую плату не установлен**.
[Готовый пакет](../demos/rt11/service/cp70/README.md).

## Источники и представление данных

DEC **EK-FP11A-UG-001**, May 1978: §1.3 (неиспользуемая половина AC),
§4.1–4.4 (форматы и FPS), §5.1–5.2 (AC и illegal mode), таблица 5-2
на печатных 5-6/5-8 и §5.3.3–5.3.4 на 5-11 (Load/Store).
[Первичный скан DEC](https://www.bitsavers.org/www.computer.museum.uq.edu.au/pdf/EK-FP11A-UG-001%20FP11-A%20Floating%20Point%20User%27s%20Manual.pdf),
SHA256 `be6446903afc3d6ca0e1b0efed85301853979e3a1c5631c8a6e707165c63ecf6`.
Порядок autoupdate/abort использует выбранный DCJ11-профиль, описанный
в [CP69](fp11-memory-cp69.md); MMU и режимные пространства не добавлялись.

AC хранится как четыре 16-bit слова в порядке от старшего к младшему.
В памяти младший байт каждого слова находится по меньшему адресу, но
**старшее слово числа идёт первым**. Это не IEEE-754 и не little-endian uint64.
В первом слове bit 15 — знак, bits 14:7 — exponent со смещением 128,
bits 6:0 и последующие слова — fraction. F использует два слова и 23
хранимых бита fraction, D — четыре слова и 55 бит; нормализованная ведущая
единица не хранится. Например, четыре слова `040200,0,0,0` представляют 1.0.

Перенос не выполняет округление, преобразование формата или нормализацию.
FPS.FT и FPS.FL на него не влияют. При FD=0 меняются только старшие два
слова destination AC: младшие два **сохраняются**, в том числе при self-copy.
SETF/SETD меняют только режим, сами данные AC не преобразуют.

| Команда | Opcode, восьмеричный | Действие |
|---|---|---|
| LDF / LDD | 172400 + AC×100 + FSRC | Копирует F/D operand в AC0–AC3; обновляет FPS.NZVC |
| STF / STD | 174000 + AC×100 + FDST | Копирует AC0–AC3 в operand; сохраняет FPS.NZVC |

В mode 0 FSRC/FDST выбирает AC0–AC5, а не CPU Rn. В двухбитном поле AC
доступны только AC0–AC3. AC6/AC7 в mode 0: AC и CPU-регистры не меняются,
FER=1, FEC=2, FEA=адрес opcode; при FID=0 выполняется USER trap 244.

LDF устанавливает FN по знаку, FZ по нулевому exponent, сбрасывает FV/FC.
Остальные биты FPS и полный CPU PSW сохраняются. Поэтому «грязный ноль»
с exponent=0 и ненулевой fraction копируется побитно и даёт FZ=1;
отрицательный ноль даёт одновременно FN/FZ, если перенос разрешён.
STF не проверяет значение и сохраняет весь FPS.

## Адресация и исключения

Все word addressing modes используют прежнюю EA-процедуру. Прямая
autoincrement/decrement адресация меняет R0–R6 на **4 байта для F / 8 для D**,
включая SP. Deferred modes 3/5 изменяют указатель на два байта. Indexed
displacement всегда один word, PC-relative использует PC после extension.

Mode 2/R7 — **одно immediate word в обоих форматах**: PC += 2.
LDF дополняет старшую половину AC нулевым вторым словом, сохраняя младшую
половину; LDD дополняет нулями все три оставшихся слова. STF/STD immediate
пишут только старшее слово AC в instruction stream. Mode 3/R7 — absolute,
а mode 4/R7 уменьшает PC на 4/8. `@-(PC)` здесь, как в CP69, даёт нечётный
pointer из самого opcode и заканчивается bus/address trap.

LDF читает все слова в частный восьмибайтный FBUF и только затем меняет AC
и FPS. Любая ошибка extension/pointer/data, включая последнее слово D,
сохраняет прежние AC/FPS и вызывает USER trap 004. STF пишет по словам:
при отказе поздней записи более ранние записи остаются видимыми. Pending
autoupdate R0–R6 на bus fault не фиксируется. PC updates и уже прочитанные
extension words следуют контракту CP69; ошибка не превращается в FP exception.

Undefined variable проверяется при **чтении памяти/immediate**: sign=1,
exponent=0. Перенос из AC и STF этой проверки не выполняют.

| FIUV | FID | LDF из памяти с отрицательным нулём |
|---:|---:|---|
| 0 | любой | Побитная загрузка, обновление FN/FZ, прежние FEC/FEA |
| 1 | 0 | AC и FPS.NZVC прежние; FER=1, FEC=14, FEA=opcode; trap 244 |
| 1 | 1 | Те же error registers и отмена загрузки, но без trap |

В отличие от bus fault, FP exception **фиксирует autoupdate**. Если operand
был `(SP)+`, trap frame строится уже относительно обновлённого SP. IRQ и
запрос ODT ждут завершения переноса/исключения и восстановления USER context.
Повторный fault при построении trap frame остаётся терминальным, как в CP68.

Opcode перечитывается из USER[CPC−2], поэтому требуется стабильный RAM
instruction stream. Volatile I/O как источник самого opcode не поддерживается.
Физические обращения operand идут через прежнюю карту USER RAM/периферии.

## Два расхождения с существующим oracle

Сравнение использует **неизменённый** `core/pdp11_fp.c` через `core_step`.
Hash файла: `ad5ef37ef0ee8fa9221632ad903224599759cf95a4bcbe4f5dfd77e0870f2220`.
Обнаружены два дефекта этого oracle; они не перенесены в FP11 firmware:

1. `GeteaFP`, mode 0, reg>=6: после постановки FP trap устанавливается
   `fAbort`, и финальный код `fp11` не выполняет pending trap. §5.2 DEC
   прямо требует trap при FID=0. Для 128 строк ожидаемый frame задаётся
   отдельно, после проверки фактического ошибочного поведения oracle.
2. `ReadFP`, immediate: отсутствует проверка `fAbort` после `ReadWI`.
   После отказа чтения opcode 172427 и его AC/F/D вариантов oracle меняет
   AC/FPS уже после bus trap. В восьми строках отдельно задано сохранение
   прежнего AC/FPS при незавершённой загрузке. Bus frame и PC берутся из oracle.

Вектора содержат явный marker: последнее слово 0 — differential, 1 —
manual invalid AC, 2 — manual immediate fault. **136 manual cases не включены
в число differential cases.** Изменять общий emulator в этом checkpoint не
потребовалось; исправление его двух дефектов остаётся отдельной задачей.
Reproducer и точный oracle включены в архив; файл oracle не подменяется
исправленной копией и не используется как подтверждение FP11-A арифметики.

## Размер, cold init и установка

`firmware/fp11/FP11.MAC`, версия `00.03`, собирается DEC MACRO/LINK под RT-11
в SIMH. Модуль **абсолютный**, CP67 format version 2 / ABI 3, без relocation.
`microasm11` не используется. Cold init очищает BSS с readback, устанавливает
FP vector 010/012, публикует ready последним и сохраняет ODT/debug-ready.

| Область | Восьмеричные адреса / размер |
|---|---|
| Immutable code | 040000–041775, **1022 байта / 511 PDP-11 слов** |
| FPS / FEC / FEA | 041776 / 042000 / 042002 |
| AC0–AC5 | 042004–042063, 48 байт |
| Saved R0–R7 и PSW | 042064–042105 |
| Fault bookkeeping, pending update, ACPTR/FLEN/FBUF | 042106–042141 |
| Private stack | 042142–042341, 128 байт |
| Полная аллокация | 040000–042341, **1250 байт** |
| Свободный непрерывный остаток до I/O page | 042342–157777, **39710 байт** |

От CP69 добавлено 288 байт кода и 12 байт BSS. Все 228 байт mutable state
очищаются при cold init и исключены из checksum. FP11.BIN — **1536 байт /
три RT-11 блока**; checksum суммы 511 слов — `026226` (11414 decimal).
Таблица модулей описывает immutable length; весь диапазон до MEMEND нужно
резервировать отдельно. Автоматического BSS allocator нет.

Для обновления установленного FP11 сначала OFFn в UJMOD и cold RESET,
затем новый FP11.BIN через UJMOD и ещё один cold RESET. ODT/SDBOOT сохраняются.
Индексы берутся из STATUS. UART ESC в начале cold start выбирает встроенный
bootstrap с обходом всех модулей. [Команды и связанный FPTST](../demos/rt11/service/cp70/README.md).

## Проверки и воспроизведение

| Проверка | Результат |
|---|---|
| Sync ROM | **41812 cases / 2961696 checks**, из них 41676 differential и 136 manual — PASS |
| Logic decode | **12080 cases / 862956 checks**, из них 136 manual — PASS |
| Vendor DP8KC | **445 cases / 31814 checks**, из них 66 manual — PASS |
| Directed sync и vendor | **31 cases / 1110 checks** каждый — PASS |
| Полная SPI FRAM модель | **4 scenarios / 450 checks — PASS** |
| RT-11/UJMOD/ODT, SPI SD/FRAM и UART waveform | **48 checks / 521238827 clocks / 3285 UART bytes — PASS** |

Sync проверяет все корректные F/D кодировки, AC0–AC5 и их неизменяемые части,
все modes/Rn, исходные FPS и разные raw operands, self-copy, dirty/negative zero,
FIUV/FID, FL/FT, 3568 injected faults и 836 odd-address probes. Сравниваются
FPS/FEC/FEA, весь CPU PSW, R0–R7, все 24 слова AC и полный журнал USER-записей.
ACK delays 0–3 clocks. Это не полный перебор 64-bit operands или всех адресов.

Logic сохраняет все injected faults/odd probes и каждый opcode в F/D,
сокращая набор начальных состояний. Vendor использует отдельную выборку
445 случаев: AC/mode/F/D, invalid AC, representative R2/R7 faults и odd probes.
Она не заменяет полный sync набор. Directed regression сохраняет прежние
faults/IRQ/trace/ODT случаи и добавляет IRQ и ODT внутри четырёхсловной загрузки,
undefined variable с `(SP)+` и проверку приоритета FP trap перед queued IRQ.

SPI regression проверяет оба порядка инициализации ODT/FP, повторный RESET
после загрязнения BSS, отказ checksum FP с сохранением ODT; измеряет 79
операций. RT-11 тест использует настоящие UJMOD/MACRO/LINK и FPTST, пошаговые
команды UART ODT, возврат в RT-11, DIR, повторный cold reset и OFF FP.
ESC window сокращено двумя константами только в тестовом firmware ROM.

Из `uJ11-fpga`, последовательно:

```sh
python3 tools/test_fp_transfer_cp70.py
python3 tools/test_fp_transfer_cp70.py --mode logic
python3 tools/test_fp_transfer_cp70.py --mode vendor
python3 tools/test_fp_events_cp70.py
python3 tools/test_fp_events_cp70.py --vendor
python3 tools/test_fp_board_cp70.py
python3 tools/run_fp11_rt11_cp70.py --out build/cp70-fp11/rt11-new
python3 tools/verify_fp70.py
```

Зависимости как в CP69: Verilator, Icarus/Lattice models, C compiler,
SIMH `pdp11`, `lsi11/rt11tool` и базовый `lsi11-fpga/images/rt11v503.dsk`.
Образ копируется в частный рабочий каталог; для RT-11 нужен новый `--out`.
[Архив CP70](../tb/reports/cp70/archive.json) содержит inputs, native assembly,
вектора, логи, UART и CSV. `verify_fp70.py --current` сверяет текущие inputs.

## Скорость и FPGA

Таблица — core clocks полной SPI FRAM модели на прежнем контроллере.
Начало: первый USER opcode request. Конец: окончание START с восстановленным
USER context. Измеряется AC2, mode 0 использует AC5, остальные — R2;
immediate — одно слово `040200`, indexed displacement — `002000`.
Исходные данные заданы в тесте; zero/sign-dependent paths могут менять время.

| Operand | LDF | LDD | STF | STD |
|---|---:|---:|---:|---:|
| AC5 | 17114 | 17695 | 16730 | 17311 |
| (R2) | 18259 | 19476 | 15914 | 16867 |
| (R2)+ | 19706 | 20923 | 17361 | 18314 |
| @(R2)+ | 19953 | 21170 | 17608 | 18561 |
| -(R2) | 20082 | 21299 | 17361 | 18314 |
| @-(R2) | 19953 | 21170 | 17608 | 18561 |
| X(R2) | 19140 | 20357 | 16795 | 17748 |
| @X(R2) | 19692 | 20533 | 16971 | 17924 |
| immediate | 18889 | 19470 | 16415 | 16474 |

Вход до первого запроса FP handler — прежние **316 clocks**, START
fetch→USER return — **177 clocks**. LDF/STF family занимает в этой выборке
около **538–721 µs** при nominal 29,56 MHz. Это стоимость переноса, не
оценка будущих FP-вычислений. Bus beats и ideal RAM timings хранятся отдельно
в CSV; ideal RAM начинается с выхода bootstrap START перед opcode fetch.

Управляющие register LDFPS/STFPS теперь занимают 11755/11893 clocks:
ещё **+380 clocks от CP69** из-за dispatch на общую EA-процедуру. Special
CFCC/SETF/SETD/SETI/SETL по времени не изменились. Оптимизация register path
остаётся отдельной задачей после функционального baseline.

Все 53 обычных файла synthesis manifest CP67b совпали по SHA256;
это также проверяет архиватор. RTL, microstore и FPGA firmware ROM не изменены.
Прирост **0 LUT / 0 FF / 0 EBR / 0 microinstructions**. Прежний измеренный
baseline CP67b: **1244 LUT / 381 FF / 7 EBR / Fmax 32,032 MHz**,
1005/1024 microinstructions; свободны 36 LUT и 0 EBR. Нового синтеза нет.
Следующий этап — операции над знаком/нулём и сравнение, затем арифметика
и преобразования с отдельными FP11-A rounding/exception tests.
