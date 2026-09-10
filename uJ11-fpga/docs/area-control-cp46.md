# CP46 — управление relocation и классификация PA22

**Четыре варианта отклонены по площади.** Лучшей основой остаётся CP45k:
**1302 LUT / 359 FF / 7 EBR / 652 slices**, MAP FAIL. Ни один вариант CP46
не сокращает LUT; границу HC1200 по-прежнему превышаем на 22 LUT / 12 slices,
ещё без резерва для protection/restart. Production CP40h, принятый APR
experiment CP43d и физическая плата CP29a сохранены.

Изменения создаются только в `build/cp46-control/` генератором
`tools/build_control_cp46.py`. Native CPU/ALU, периферия, FRAM transport,
firmware и microcode **954/1024×36 v12** не менялись. Новых возможностей MMU
в этом checkpoint нет; FP11 остаётся отложенным, FIS сохранён.

## Измеренные варианты

Diamond 3.14.0.75.2, LCMXO2-1200HC-4SG32C, прежние pins/strategy,
clock constraint 29.56 MHz. External pin delays не заданы.

| Gate | Изменение относительно CP45k | LUT4 | FF | EBR | Slices | Результат |
|---|---|---:|---:|---:|---:|---|
| CP45k | Исходный `narrow-rom` | 1302 | 359 | 7 | 652 | MAP FAIL |
| [CP46a](../synth/reports/cp46a/result.json) | PA17 + qualified RAM/I/O flags | 1316 | 356 | 7 | 659 | MAP FAIL |
| [CP46b](../synth/reports/cp46b/result.json) | Уравнения двух phase bits | 1315 | 359 | 7 | 661 | MAP FAIL |
| [CP46c](../synth/reports/cp46c/result.json) | Qualified APR address, прямые write lanes | 1317 | 359 | 7 | 661 | MAP FAIL |
| [CP46d](../synth/reports/cp46d/result.json) | Сочетание A+B+C | 1317 | 356 | 7 | 659 | MAP FAIL |
| [CP46e](../synth/reports/cp46e/result.json) | Неизменённый CP45k в окружении CP46 | 1302 | 359 | 7 | 652 | MAP FAIL |

PAR/TRACE после отказа MAP не выполнялись. Fmax и аппаратные instructions/sec
не получены; прежние 31.470 MHz production и 30.866 MHz APR нельзя переносить
на relocation prototype. Все gate reports содержат snapshots и hashes
фактических входов. Финальные input hashes A–E сверены с текущими файлами. Контроль E повторил
CP45k по LUT/FF/EBR/slices: перенос неизменённых RTL в окружение CP46 сам
по себе не изменил площадь.

### A: классы физического адреса

Bridge сначала вычисляет полный PA22 и признаки RAM/I/O/NXM. Board bus
получает PA[16:0] и два qualifier. Верхние 64 КиБ FRAM сохраняются;
PA за пределами `000000..01ffff` hex и canonical I/O `3fe000..3fffff`
не превращается в low-memory alias. Низкие биты APR CSR декодируются с
canonical prefix, а запрос квалифицируется признаком настоящей I/O page.
Bootstrap overlay/release также проверяет RAM qualifier. Private RK bypass
сохраняет **точный** диапазон VA160000..160477 octal.

Эта форма позволила synthesis удалить три регистра, но добавила 14 LUT.
Сами зарегистрированные region flags и перенесённые условия выбора оказались
дороже экономии старших адресных bits в полном top. Цену нельзя оценивать
только по числу сохранённых FF.

### B: phase equations

Состояния IDLE/CAPTURE/MAPPED/BYPASS и их двоичные коды сохранены.
Вместо условных присваиваний всего `phase` используются отдельные уравнения
двух bits; захват суммы PAR+block offset выделен в отдельный clocked block.
Lookup grant, ACK, held request, reset и удержание выбранного режима во время
записи MMR0 сохраняют прежние такты. Несмотря на более короткую запись,
полный top стал на 13 LUT больше.

### C: порт APR EBR

Вместо priority address mux проверена сумма двух masked addresses: lookup
при `lookup_grant`, CSR при `memory_enable`. Эти grants взаимоисключающие.
Адрес при выключенном EBR не является наблюдаемым. Lookup проходит только
в READ, где обе write lanes уже равны нулю, поэтому дополнительный mux
`lookup_grant ? 0 : lanes` убран. Итог — +15 LUT в полном top.

Это функционально корректные, но невыгодные преобразования. Они остаются
в экспериментальном генераторе для воспроизведения и не принимаются в
рабочую конфигурацию.

## Проверки

`tools/check_control_cp46.py` сравнивает реальные RTL cones:

| Граница | Доказано | Непроверенных точек | Намеренная ошибка обнаружена |
|---|---:|---:|---|
| Полный sequential bridge | 70 | 0 | Нет перехода CAPTURE → MAPPED |
| APR controller + наблюдаемые RAM pins | 55 | 0 | Write enable при lookup |
| Bus response/selectors + boot release | 51 | 0 | RAM alias через PA21 |

APR proof использует произвольные входные данные RAM. Сравниваются enable,
address при enable, write lanes при enable, данные только записываемых lanes,
а также внешние ready/busy/grant. Unqualified RAM address и внутренний
canonical APR decode не являются границами эквивалентности: их значения
на отключённых путях намеренно отличаются. Первые версии harness сравнивали
и эти значения; после уточнения наблюдаемой границы positive proof проходит,
а все три negative controls по-прежнему отклоняются.

Icarus проверяет **131072** комбинации известных address/control/state с X/Z
в выбранных и невыбранных device words. C-oracle corpus из CP44 повторно
проверен по сохранённому hash: **262144 адреса / 196608 stalled lookup edges**,
все PAR16, режимы 16/18/22, held changes и reset. Strict Verilator lint нового
bridge проходит без warnings.

Интеграционные тесты комбинированного варианта D:

| Проверка | Результат |
|---|---|
| CPU portable: 4096 words × 8 pages × 18/22 | 67468380 clocks, 590096 PAR reads, 8 control beats |
| CPU vendor EBR: 4 words/page/mode | 97692 clocks, 848 PAR reads, 8 control beats |
| Portable/vendor edge cases | Wrap/NXM/I/O, MOVB lane/sign, odd vector4, high stack/opcode/immediate — PASS |
| APR portable и vendor EBR, каждый | 1144373 commands, 2288746 coherent lookup reads; CSR collision/release — PASS |
| Full bus | 32 transaction checks, 6291456 PA/private-bypass combinations — PASS |
| Cold RT-11FB + DIR | 415159611 clocks; все counts и UART совпали с CP45 |

CPU записывает/читает каждое слово верхних 64 КиБ FRAM и сравнивает snapshot
нижнего банка. Cold FB: 4311823 retirements, 5675704 reads / 489280 writes,
3980328 FRAM transactions, 300 RK commands, 576 timer edges, 3270 UART bytes,
162 SD reads / 6 RAM-overlay writes. Mapped/high-FRAM beats — 526239/65664,
MMR0 writes — 4. Дополнительных тактов нет; +2 clocks/mapped beat сохраняются.

MMU-enabled private ROM/DMA beats — **0**. Это не проверка active-MMU RK
transfer и не запуск XM. FIS corpus отдельно не повторялся: его CPU/datapath/
microcode неизменны; новые control paths проверены перечисленными тестами.
Vendor compile warnings ограничены прежними implicit nets модели DP8KC и
унаследованным timescale reference UART; portable build/lint чистые.

## Проверка предупреждений Synplify о драйверах

В CP45 и CP46 присутствуют BN161. Лог ограничен первыми **100** диагностическими
сообщениями; это **не** установленное полное число предупреждений. Исторические
raw reports не редактировались, blanket waiver не добавлялся.

`tools/check_edif_drivers_cp46.py` разбирает **конечный EDIF**, directions
портов, cell/instance references, включая `rename`, `array`, `member`.
Для CP45k: **54 cells / 3453 nets**, для CP46d: **54 cells / 3442 nets**.
В обоих — **0 сетей с несколькими направленными drivers**, по 8 INOUT nets
учтены отдельно. Это структурная проверка: она не доказывает отсутствие
электрического конфликта на двунаправленных выводах и не заменяет gate-level
CPU equivalence, PAR/TRACE или аппаратный тест.

В каждом netlist есть семь недрайвленных CIN первых CCU2D. Они не считаются
автоматически нулём. По официальной модели Diamond `machxo2/CCU2D.v`:

```text
cout_0 = (~prop_0 & gen_0) | (prop_0 & CIN)
sum_0  = prop_0 ^ (CIN & gen_0_0)
```

Проверяющий код перебирает все assignments неконстантных A0..D0 с реальными
INIT0 и VLO/VHI connections и доказывает `prop_0=0`. Если INJECT1_0=NO,
S0 обязан быть неиспользуемым. Таким образом CIN не влияет на наблюдаемые
S1/COUT; других необъяснённых floating nets нет. Model SHA сохранён, vendor
исходник не включён в публикуемый архив.

Три мутации каждого настоящего EDIF обнаруживаются: добавленный второй driver,
удалённый driver обычного input и INIT0, делающий открытый CIN наблюдаемым.
Оригинальные и изменённые EDIF сохранены с gzip и hashes в
[архиве CP46](../tb/reports/cp46/). Это проверяет сам audit и не утверждает,
что все промежуточные BN161 были классифицированы по одному.

## Ограничения и следующий шаг

Kernel unified PAR relocation всё ещё не имеет PDR protection/length/W,
MMR1/2, hardware MMR0 fault/page metadata, abort250/freeze/restart,
mode/SP switching, separate I/D, CSM/MAP и high RK DMA. RT-11XM из
`../lsi11/disks/rt11v5.3/system.dsk` **ещё не запускалась**, backing images
не изменены. FPGA не программировалась; на плате остаётся CP29a.

Следующий ограниченный area experiment — SPI FRAM transport и его byte/state
mux на основе CP45k: это около 82 ORCALUT4 в hierarchical Synplify report,
не самостоятельная аддитивная MAP-оценка. Сначала нужно доказать неизменность
SPI mode 0, WREN/CS, двух banks, byte lanes, held request и ACK latency,
затем измерить полный HC1200 top. Менять корректность ради LUT нельзя.

Воспроизведение с подготовленными CP44/CP45 build inputs и vendor models:

```sh
python3 tools/build_control_cp46.py
python3 tools/check_control_cp46.py
python3 tools/check_control_cp46_units.py
python3 tools/check_control_cp46_system.py --variant combined --suite cpu
python3 tools/check_control_cp46_system.py --variant combined --suite cpu --vendor --words 4
python3 tools/check_control_cp46_system.py --variant combined --suite cpu --edges
python3 tools/check_control_cp46_system.py --variant combined --suite cpu --vendor --edges
python3 tools/check_control_cp46_system.py --variant combined --suite apr
python3 tools/check_control_cp46_system.py --variant combined --suite apr --vendor
python3 tools/check_control_cp46_system.py --variant combined --suite bus
python3 tools/check_control_cp46_system.py --variant combined --suite board
```

Проверка EDIF (оригиналы можно восстановить из `tb/reports/cp46/*.edi.gz`):

```sh
python3 tools/check_edif_drivers_cp46.py build/cp46-baseline.edi --output build/cp46-baseline-drivers.json --negative-controls build/cp46-edif-negative
python3 tools/check_edif_drivers_cp46.py build/cp46-combined.edi --output build/cp46-combined-drivers.json --negative-controls build/cp46-combined-edif-negative
```

На Diamond — `tools/checkpoint_control_cp46.py` с новым именем gate и
`--variant classified|phase|apr|combined|baseline`. Не перезаписывать исторические
gates. `tools/record_cp46.py` связывает результаты с точными inputs и сохраняет
[verification manifest](verification-cp46.json); запускать только с источниками
этого checkpoint. Исторические harness PASS lines сохраняют префикс CP44,
новые manifests указывают фактические CP46 sources и оба driver hashes.
