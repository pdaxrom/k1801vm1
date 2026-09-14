# CP72: прямые переносы FP11 между аккумуляторами

2026-09-14. Оптимизирован mode 0 **LDF/LDD и STF/STD** в программном
FP11-A. LDF больше не копирует AC сначала в FBUF, затем в destination AC.
Два слова F или четыре слова D переносятся напрямую; циклический copy
заменён фиксированной последовательностью MOV. Общая обработка autoupdate
при возврате пропускается: у mode 0 нет изменений USER Rn.

Новых команд нет: прежние **22 мнемоники / 1189 корректных кодировок**,
32 illegal-AC сочетания. Семантика FP11-A, FIS, addressing/exception profile
и ограничения остаются [как в CP71](fp11-unary-cp71.md).
ADD/SUB/MUL/DIV и преобразования ещё не реализованы.
CP72 проверяется в симуляции; **на физическую плату модуль не установлен**.
[Готовый пакет](../demos/rt11/service/cp72/README.md).

## Что изменено и почему это корректно

Проверка mode-0 operand выполняется до доступа к AC: одно unsigned
сравнение отклоняет AC6 и AC7. Допустимы AC0–AC5; двухбитное поле
load/store accumulator по-прежнему выбирает AC0–AC3. Проверка общего
диапазона заменяет два сравнения CP71, компенсируя дополнительный выбор
прямого пути LDF без замедления unary/CMP.

Прямое копирование использует уже проверенные указатели на AC в HALT FRAM.
Первое слово источника сохраняется в R0 до конца переноса: по нему LDF
обновляет FPS.NZVC после записи всех слов. STF сохраняет FPS целиком.
В F mode младшие два слова destination AC не читаются и не записываются.
Self-copy допустим: каждое слово прочитано до записи по тому же адресу.
AC sources не вызывают FIUV, в соответствии с FP11-A.

FBUF остаётся для memory operands, unary и CMP. Memory faults, partial
USER writes, отложенные R0–R6 autoupdates и немедленные PC updates не менялись.
Прежний FP11-A TST flags-before-undefined-variable и особый CMP двух нулей
сохранены. RTL удерживает запрос IRQ/пульта до возврата из service mode;
новые направленные тесты подают их при первой записи destination AC.

## Измеренный выигрыш

Та же программа и полная SPI FRAM модель, что в CP71. Начало интервала —
первый USER opcode request, конец — завершение START с восстановленным USER
context. Числа — **core clocks**, без изменения частоты или memory controller.

| Команда | CP70 | CP71 | CP72 | Экономия от CP71 |
|---|---:|---:|---:|---:|
| LDF AC5,AC2 | 17114 | 21369 | **17662** | 3707 / **17,35%** |
| LDD AC5,AC2 | 17695 | 22472 | **18120** | 4352 / **19,37%** |
| STF AC2,AC5 | 16730 | 17955 | **16845** | 1110 / **6,18%** |
| STD AC2,AC5 | 17311 | 18536 | **17303** | 1233 / **6,65%** |

Число CPU memory transactions соответственно уменьшилось **337→278,
354→285, 283→265, 292→272**. Это CPU bus beats, а не отдельные SPI-биты.
При nominal 29,56 MHz новые значения — примерно **597,5; 613,0; 569,9; 585,4 µs**.
Вход до handler — прежние 316 clocks; START fetch→USER return — 177.

Остальные **165 из 169 измеренных операций** совпали с CP71 по всем CSV
полям, включая clocks и bus beats. Среди них все memory addressing modes
LDF/STF, управляющие команды и CLR/TST/ABS/NEG/CMP. Это сравнение конкретных
тестовых operands, не worst-case timing для произвольного FPS и значения AC.

Регрессия от CP70 для LDF/LDD AC5 сокращена до **+548/+425 clocks**, STF
остаётся +115, STD стал на 8 clocks короче. Общий dispatch и memory LDF/STF
ещё требуют оптимизации: память по-прежнему медленнее CP70 на 2341…2400
для LDF/LDD и на 1652 для STF/STD. Полного устранения регрессии не заявляется.

[Сравнение с хешами источников](../tb/reports/cp72/comparison.json) создаёт
`tools/compare_fp72.py`. Оно отвергает замедление оптимизируемых четырёх
случаев, любое изменение остальных 165 строк и изменение интервала измерения.
Старые CSV проверяются по архивным хешам CP70/CP71.

## Размер и сохранённый hardware

`FP11.MAC` версия **00.05**, сборка DEC MACRO/LINK под RT-11 в SIMH.
Абсолютный retained module CP67 format 2 / ABI 3, без relocation.

| Область | Восьмеричные адреса / размер |
|---|---|
| Immutable code | 040000–042561, **1394 байта / 697 PDP-11 слов** |
| FPS / FEC / FEA | 042562 / 042564 / 042566 |
| AC0–AC5 | 042570–042647, 48 байт |
| Saved R0–R7 и PSW | 042650–042671, 18 байт |
| Bookkeeping и FBUF | 042672–042727 |
| Private stack | 042730–043127, 128 байт |
| Полная аллокация | 040000–043127, **1624 байта** |
| Непрерывный остаток до I/O page | 043130–157777, **39336 байт** |

От CP71 добавлено **30 байт кода**, BSS/stack прежние **230 байт**.
FP11.BIN остаётся **2048 байт / четыре RT-11 блока**; checksum суммы
697 слов — `144206` (51334 decimal). Mutable state исключено из checksum
и очищается cold init. Таблица хранит immutable length; весь диапазон до
MEMEND=`043130` резервируется отдельно, автоматического BSS allocator нет.

Дополнительные 15 слов увеличили длительность cold boot сценариев на
5475 clocks: ODT/FP/SDBOOT — **3027040**, bad FP checksum — **2981629**,
FP плюс тестовая программа — **590829**. Проверка checksum/инициализация
работают через прежний generic table walker, ODT/SDBOOT сохраняются.

Все **53 обычных файла** synthesis manifest CP67b совпали по SHA256.
Прирост **0 LUT / 0 FF / 0 EBR / 0 microinstructions**, нового синтеза нет.
Прежний измеренный baseline: **1244 LUT / 381 FF / 7 EBR / Fmax 32,032 MHz**,
1005/1024 microinstructions. Свободны 36 LUT и 0 EBR. MMU, RTL и FPGA ROM
не менялись; `microasm11` не используется и не изменялся.

## Проверки

Sync и logic используют **побайтно те же вектора, что CP71**, с прежними
ожидаемыми AC, FPS/FEC/FEA, R0–R7, PSW, trap frames и USER write log.
Fixture generator `reference_fp_unary_cp71.c` и общий emulator неизменны.
Это позволяет проверять оптимизацию без подгонки ожидаемых результатов.

| Проверка | Результат |
|---|---|
| Sync ROM | **99104 cases / 6947312 checks**, 1928 manual — PASS |
| Logic decode | **22748 cases / 1621848 checks**, 1654 manual — PASS |
| Vendor DP8KC | **467 cases / 32176 checks**, 128 manual — PASS |
| Directed sync и vendor | **35 cases / 1254 checks** каждый — PASS |
| Полная SPI FRAM модель | **4 scenarios / 632 checks**, 169 измерений — PASS |
| RT-11/UJMOD/ODT, SPI SD/FRAM, UART waveform | **43 checks / 531124415 clocks / 3476 UART bytes — PASS** |

В полном наборе 97176 differential и 1928 отдельных ожидаемых результатов:
256 invalid-AC, 22 immediate-read abort, 1210 выбранных bus-fault commit
случаев и 440 отличий TST FP11-A от J-11. Их смысл и границы изложены в CP71;
они не смешиваются с прямым differential comparison. Все 7452 injected
faults и 1540 odd-address probes сохранены в sync/logic.

Vendor sample целенаправленно проверяет каждый floating mode-0 opcode
в F/D, AC6/7 с обоими FID, дополнительные sign/zero/self-copy случаи
LDF/STF и memory transfers R2/R7 во всех modes. Он не повторяет полный
набор memory fault injections CP71; они сохранены в sync/logic и directed suite.
Прежние 33 directed cases дополнены LDD с IRQ и STD с запросом пульта
**между записями слов AC**. Проверяется полный результат, FPS, CPU-регистры
и PC/PSW последующего IRQ/ODT. В F mode сохранение младших слов проверяется
основным набором на всех допустимых парах AC.

FPTST из CP71 сохранена побайтно, включая native SAV. RT-11 тест вновь
устанавливает ODT/SDBOOT/FP через UJMOD, выполняет 11 UART ODT STEP,
проверку результатов самой FPTST, возврат, DIR, cold reset и OFF FP.
Сокращено только начальное ESC window тестовой ROM; production hardware прежний.

Из `uJ11-fpga`, последовательно:

```sh
python3 tools/test_fp_paths_cp72.py
python3 tools/test_fp_paths_cp72.py --mode logic
python3 tools/test_fp_paths_cp72.py --mode vendor
python3 tools/test_fp_events_cp72.py
python3 tools/test_fp_events_cp72.py --vendor
python3 tools/test_fp_board_cp72.py
python3 tools/run_fp11_rt11_cp72.py --out build/cp72-fp11/rt11-new
python3 tools/compare_fp72.py
python3 tools/verify_fp72.py
```

Зависимости и частная копия RT-11 образа — как в CP71. Для RT-11 нужен новый
`--out`. [Архив CP72](../tb/reports/cp72/archive.json) содержит native assembly,
sources, vectors, CSV и UART. `verify_fp72.py --current` также сверяет текущие
inputs. Физическая установка и проверка по UART остаются отдельным шагом.
Следующий функциональный этап — ADD/SUB F/D с rounding/exception tests.
