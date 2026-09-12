# Benchmarks: CP59 и предыдущие этапы

## CP59d — улучшен timing без изменения количества тактов

Все девять portable/vendor full-board workloads совпали с CP56 по каждому
счётчику. R,R — **23,5625 CPI**, около **1,255 млн instructions/s** при
прежних номинальных 29,56 MHz. Служебный вход 258 clocks, START 120,
STEP 119 до запроса следующего opcode. Cold RT-11FB + DIR —
**173379163 clocks**, те же 3270 UART bytes.

HC1200: **1239 LUT / 343 FF / 6 EBR / Fmax 32,531 MHz**.
Пройден gate 31,824 MHz с FRAM budgets, но рабочая частота не повышалась;
это сохранение производительности и увеличение запаса timing.
[Сырые счётчики](../tb/reports/cp59d/bench-results.json),
[измерения и ограничения](timing-cp59.md).

## CP58a — STEP/SEL004, без изменения гостевых счётчиков

Девять full-board portable/vendor workloads сохранили все счётчики CP56:
R,R — **23,5625 CPI**, cold RT-11FB+DIR — **173379163 clocks**, UART совпадает.
Служебный вход — 258 clocks, START — 120, STEP — 119 до запроса следующей
opcode fetch на полном SPI FRAM bus (завершение fetch в интервал не входит).
Simulation и full-board Diamond: выбран CP58a, 1246 LUT / 342 FF / 6 EBR /
Fmax 30,743 MHz при nominal gate 29,56 MHz. Плата остаётся CP56a.
[Сырые счётчики](../tb/reports/cp58a/bench-results.json),
[контракт и ограничения](service-bank-cp58.md).

## CP57e — служебный банк, обычный guest code не замедлился

Все девять portable/vendor full-board workloads совпали по **каждому** счётчику
с CP56. MOV/ADD/CMP/mixed R,R: **6032 clocks / 256 instructions = 23,5625 CPI**
(около 1,255 млн instructions/s при номинальных 29,56 MHz). Memory/SPI beats
и cold RT-11FB+DIR также прежние: **173379163 clocks**, 3270 UART bytes,
побайтное совпадение с CP56.

На полном bus с SPI FRAM измерены служебные переходы: **258 clocks** от
commit ENTER до первого запроса opcode handler; **120 clocks** от первой
микрокоманды START до запроса следующего guest opcode без pending IRQ/trace.
Эти интервалы не включают завершение самих opcode fetch и исполнение handler.
Контекст читается/пишется в верхней FRAM; это цена редких служебных входов,
не дополнительная задержка каждой гостевой инструкции. При возвращении с T
testbench получил 380 clocks: интервал включает построение trace frame.

Полный synthesis: 1260 LUT/342 FF/6 EBR/Fmax31,982 MHz. Эти результаты —
simulation и Diamond, CP57 на плату не прошивался. [Исходные счётчики](../tb/reports/cp57e/bench-results.json),
[проверки и определения интервалов](verification-cp57.json).

## CP56 — FRAM SCK 14,78 → 29,56 MHz, simulation

CPU остаётся на 29,56 MHz. Девять portable и девять vendor workloads
совпали; memory/SPI operation counts те же. R,R **40,0625 → 23,5625 CPI**
(**1,700×**); memory workloads **1,743–1,768×**; stack **1,739×**;
BR self **1,814×**. Cold RT-11FB + DIR: **288686609 → 173379163 clocks**
(**1,665×**), каталог совпал. Реальный MAP/PAR: **1184 LUT / 339 FF /
6 EBR / 31,996 MHz**. Скорость программы пока измерена в simulation;
CP56a установлен по запросу пользователя; отдельного аппаратного измерения
этих workloads пока нет. [Таблица CP56](spi-cp56.md), [synthesis](synthesis-cp56.json),
[локальные проверки](verification-cp56.json).

## CP55 — native shared FRAM receive/result

Девять portable и девять vendor full-board workloads: все counters CP54b
сохранены, R,R **40,0625 CPI**. Новый cold RT-11FB + DIR — **288686609
clocks**, те же 300 RK/467 timer/3270 UART/162 SD reads/6 writes; counters
и raw UART точно совпали. Полный synthesis: **1182 LUT / 333 FF / 6 EBR /
30,827 MHz**. От CP54b −3 LUT/−8 FF, но −1,446 MHz Fmax. На штатных
29,56 MHz производительность прежняя: R,R около **737847 instructions/s**,
в 2,671× быстрее default; cold FB+DIR — в 1,229×. Это расчёт по simulation
cycles и номинальной частоте, нового измерения платы нет. Запас внутреннего
timing 1,390 ns; внешние pin delays не заданы.
[Контракт и проверки](rx-cp55.md), [manifest](verification-cp55.json),
[измерения](synthesis-cp55.json).

## CP54 — I/O/DMA decode и ACK

Два варианта, по девять portable и девять vendor EBR workloads каждый.
Все **36 измерений** точно сохранили raw counters CP53a: R,R 40,0625 CPI,
остальные workloads без изменений. Оба новых cold FB+DIR PASS, каждый
288686609 clocks; все counters и raw UART точно совпали с CP53a.
Полный synthesis выбранного CP54b: **1185 LUT / 341 FF / 6 EBR /
32,273 MHz**, −10 LUT от CP53a при той же производительности на 29,56 MHz.
Ускорение относительно native default остаётся 2,671× на R,R и 1,229×
на cold FB+DIR. [CP54](ack-cp54.md), [проверки](verification-cp54.json),
[synthesis](synthesis-cp54.json).

## CP53 — три cursor-варианта сохранили все counters CP52

По девять full-board workloads для increment/compare/both. Для каждого
побитно сравнены все числовые counters: clocks, retired instruction count,
bus beats, opcode reads, writes, request/busy clocks, CS и SCK. Все совпали
с CP52, включая **40,0625 CPI** для MOV/ADD/CMP R,R. После трёх полных
synthesis выбран CP53a: **1195 LUT / 341 FF / 6 EBR / 31,338 MHz**.
Новые девять vendor EBR workloads сохранили все portable counters.
Новый cold RT-11FB + DIR: **288686609 clocks**, 300 RK, 467 timer edges,
3270 UART bytes, 162 SD reads/6 writes. Все counters и raw UART точно
совпали с CP52. Ускорение против native baseline остаётся **1,229×**;
площадь −3 LUT от CP52b. Default и плата прежние. [CP53](cursor-cp53.md),
[ранние проверки](verification-cp53.json), [финальный gate](synthesis-cp53.json).

## CP52 — full native board с demand sequential FRAM READ

Portable и Lattice EBR models: одинаковые девять workloads. MOV/ADD/CMP
R,R и mixed ALU: **40,0625 CPI вместо 107 (2,671×)**. На 256 инструкций
4224 SCK вместо 12288; memory workloads, stack и BR self пока без ускорения.

Новый cold RT-11FB + DIR с UART wire scoreboard прошёл для baseline
и candidate: **354938300 / 288686609 clocks**, ускорение **1,229×**,
на **18,67%** меньше тактов. По 300 RK commands, 162 SD reads / 6 writes,
3270 UART bytes. У candidate 467 timer edges вместо 576; поэтому retired
instructions и bus beats закономерно отличаются — это timed OS workload,
а не фиксированный поток retirements. Текст одинаков после исключения CR;
SL упорядочивает лишний CR при redraw иначе. Обе raw UART записи сохранены.

Ошибка прежнего testbench: промежуточный SL prompt мог быть принят за
конец DIR до вывода `Files`. Исправлена проверка завершения, baseline
повторил прежнее число clocks точно. Каждый candidate FRAM beat проверен
по содержимому модели. Full-board synthesis CP52b: **1198 LUT / 341 FF /
6 EBR / 30,044 MHz**, +39 LUT/+15 FF от CP52a. Timing PASS при 29,56 MHz,
но slack только 0,544 ns. Физическая плата не измерялась; default прежний.
[CP52](fram-sequential-cp52.md), [simulation counters](verification-cp52.json),
[synthesis audit](synthesis-cp52.json).

## CP51 — warm MMU-less full-board baseline

Девять workloads по 256 retirements после guest setup и 64 warm instructions.
Применены текущие CPU, board decoder, периферия и реальный RTL SPI transport;
fixture отключает bootstrap overlay и загружает программу в FRAM model.
Это не cold RT-11 или аппаратное измерение. Stream проверяется на каждом retire.

| Workload | Clocks/instruction | FRAM busy |
|---|---:|---:|
| MOV/ADD/CMP R,R, mixed register ALU, BR self | 107,000 | 96,26% |
| MOV memory→register | 222,172 | 92,00% |
| MOV register→memory | 241,859 | 91,83% |
| MOV memory→memory | 346,203 | 93,44% |
| Stack push/pop | 233,641 | 91,33% |

Loops включают один BR на 64 инструкции, кроме BR self. Чистый register
stream: 48 SCK/instruction, 103 transport-busy clocks, расчётные
276262 instructions/sec при nominal 29,56 MHz. Выигрыш ещё не заявляется:
это исходная точка для оптимизации SPI instruction stream.
[Методика и ресурсы](resources-cp51.md), [точные counters](resources-cp51.json).

## CP50 — MMU-less профиль сохранён без изменения clocks

Default cold RT-11FB + DIR повторил CP40h: **354938300 clocks**, 3984366
retirements, 5217011 reads, 423616 writes, 3390712 FRAM transactions;
300 RK commands, 576 timer edges, 3270 UART bytes, 162 SD reads / 6 writes
в RAM overlay. UART совпал побайтно, исходный диск не изменён.
MMU-ветвь CPU проверена на 32 words/page/mode (558684 clocks) и vendor
subset 4 words/page/mode (97692 clocks). Это regression, не новая ISA
оптимизация. Новые Fmax и instructions/sec CP50 не измерены.
[Профили сборки](build-profiles-cp50.md), [manifest](verification-cp50.json).

## CP49 — counts прежние, площадь выросла

Каждый из original/split-low/equations: CPU upper-bank sweep 67468380 clocks /
590096 PAR reads, vendor subset 97692 / 848. Cold FB + DIR для split-low —
415159611 clocks, counts/UART совпали с CP47c. 18 unit runs: 548121 cycle
comparisons, 2304 reset offsets, 18432 memory transactions, все 128 КиБ.
Измерены original/split-low/equations: 1325/1335/1310 LUT при контроле
1297; все отклонены по площади. MAP FAIL, Fmax и аппаратные instructions/sec
не получены. Выигрыша в clocks нет. RT-11XM не запускалась.
[Отчёт](area-fram-binary-cp49.md).

## CP48 — clocks прежние, площадь выросла

Successors и onehot прошли полный upper-FRAM CPU sweep: каждый
67468380 clocks / 590096 PAR reads; vendor subset — 97692 / 848.
Successors cold RT-11FB + DIR: 415159611 clocks, все counts/UART прежние.
Onehot cold boot не повторялся после отрицательного area gate; CPU/vendor/
edges/bus проверены. 12 unit runs: 365414 потактных сравнений, 1536 reset
offsets, 12288 memory transactions с проверкой всех 128 КиБ.
1319/1313 LUT вместо 1297, оба варианта отклонены. MAP FAIL, Fmax и
аппаратные instructions/sec отсутствуют. RT-11XM не запускалась.
[Отчёт CP48](area-fram-state-cp48.md).

## CP47 — simulation counts прежние; нового Fmax нет

Shared-rx и combined FRAM прошли полный upper-bank CPU sweep: 67468380 clocks /
590096 PAR reads, vendor subset — 97692 / 848. Cold RT-11FB + DIR сохранил
415159611 clocks и все CP45 counts/UART. Три варианта, CLK_DIV=1/2/3:
548121 потактное сравнение SPI/ACK/data, 2304 reset offsets, 18432 memory
transactions со scoreboard всех 128 КиБ. CP47c shared-rx: 1297 LUT / 351 FF /
7 EBR / 650 slices, −5 LUT / −8 FF / −2 slices относительно CP45k.
Все четыре варианта — MAP FAIL; PAR/Fmax и аппаратные instructions/sec
не измерены. Уменьшение площади не изменило число тактов.
RT-11XM ещё не запускалась. [Отчёт](area-fram-cp47.md).

## CP46 — clocks сохранены, площадь выросла

Четыре control/region варианта: 1315–1317 LUT, все MAP FAIL; Fmax нет.
Не приняты, лучший prototype CP45k сохранён. Комбинированный вариант
повторил 67468380 clocks / 590096 PAR reads полного upper-FRAM CPU sweep,
97692 / 848 vendor subset и все cold FB + DIR counts/UART CP45.
APR portable/vendor: по 1144373 commands и 2288746 coherent lookup reads.
Новых тактов нет, hardware speedup не заявляется. RT-11XM ещё не запускалась.
[Измерения и границы проверки](area-control-cp46.md).

## CP45 — −49 LUT, прежние clocks

Relocation board: 1302 LUT / 359 FF / 7 EBR / 652 slices, MAP FAIL, Fmax нет.
Изменены bus decode/read mux, новых тактов нет. Полный upper-FRAM CPU sweep
сохранил 67468380 clocks / 590096 PAR reads; vendor subset — 97692 / 848.
Cold FB + DIR сохранил **все** CP44 counts: 415159611 clocks, 4311823
retirements, 5675704 reads / 489280 writes, 3980328 FRAM transactions,
300 RK commands, 576 timer edges, 3270 UART bytes, 162 SD reads / 6 writes.
Mapped/high-FRAM beats 526239/65664, MMR0 writes 4; UART byte-identical.

CPU/vendor edge tests, full bus, binary и four-state проверки выбранного
варианта прошли. Hardware instructions/sec и RT-11XM пока не измерены.
[Отчёт CP45](area-bus-cp45.md).

## CP44 — relocation clocks, без аппаратного Fmax

Полный CPU + SPI FRAM test: 4096 words × 8 upper-bank pages × 18/22 modes.
Direct: 67468380 clocks; microcoded: 76319820 clocks. У обоих 590096 PAR
reads и 8 control beats; разница = 15 clocks × 590096. Overhead mapped beat
без external holds — +2 / +17 clocks. Vendor subset (4 words/page/mode):
97692 / 110412 clocks, 848 PAR reads. Это directed workload, не typical ISA CPI.

Cold FB + DIR, direct prototype: 415159611 clocks / 4311823 retirements,
5675704 reads / 489280 writes, 3980328 FRAM transactions, 300 RK commands,
576 timer edges, 3270 UART bytes, 162 SD reads / 6 overlay writes. UART
совпал с CP43. Здесь уже 526239 mapped beats, 65664 upper-FRAM beats и
4 MMR0 writes, поэтому прежние CP43 counts не являются speedup baseline.
MMU-enabled private ROM/DMA beats — 0; этот сценарий ещё не проверен.

Лучший gate — 1351 LUT / 359 FF / 7 EBR / 676 slices, MAP FAIL.
Fmax, instructions/sec на HC1200 и RT-11XM results отсутствуют. Production
CP40h и плата CP29a прежние. [Полный отчёт](relocation-cp44.md).

## CP43 — MMR3 CSR, прежний cold FB workload

APR + MMR3 CP43d: 1258 LUT / 351 FF / 7 EBR / 30.866 MHz; microcode 963 words.
Cold RT-11FB + DIR: 412130048 clocks, 3987390 retirements, 5222610 reads /
424452 writes, 3397976 FRAM transactions, 300 RK commands, 663 timer edges,
3270 UART bytes, 162 SD reads / 6 overlay writes. Counts и UART совпали с CP42.

Новый MMR3 directed CPU test: 324 readbacks / 516 CSR beats / 3508 APR lookup
reads / 157988 clocks на portable/vendor. Immediate ACK B даёт 157472 clocks,
но занимает 1271 LUT против 1258 у выбранного D. D добавляет один такт на
MMR3 beat; helper по-прежнему +10 clocks/memory word без hold. Прежний APR
CPU test сохраняет 432 readbacks / 720 beats / 221988 clocks. Full FIS corpus
не перезапускался: CPU/datapath/microcode прежние. Production и hardware/XM
benchmarks не изменены. [Границы результата](mmr3-cp43.md).

## CP42 — −10 LUT с APR, прежние clocks

APR CP42d: 1248 LUT / 344 FF / 7 EBR / 30.896 MHz. Production CP40h не изменён.
Cold APR RT-11FB + DIR: 412130048 clocks, 3987390 retirements, 5222610 reads /
424452 writes, 3397976 FRAM transactions, 300 RK commands, 663 timer edges,
3270 UART bytes, 162 SD reads / 6 overlay writes. Counts и UART точно
совпали с CP40, как и FIS cycle CSV для RAM/FRAM/vendor. CPU CSR по-прежнему
221988 clocks для 432 readbacks / 720 beats. Lookup overhead — 10 clocks
на memory word без hold. Новых production/hardware/XM benchmarks нет.
[Отчёт](area-d-input-cp42.md).

## CP41 — изменений производительности нет

Четыре area-кандидата отклонены. Контрольный синтез APR повторил CP40i:
1258 LUT / 344 FF / 7 EBR / 30.254 MHz. Рабочие RTL и microcode не менялись;
новые CPU/FIS/RT-11FB benchmarks не запускались, ниже остаются измерения
CP40. Кандидат C имеет 29.387 MHz и не проходит clock constraint 29.56 MHz.
[Все эксперименты](area-sequencer-cp41.md).

## CP40 — прежние такты при меньшей площади

Production 1159 LUT / 326 FF / 6 EBR / 31.470 MHz; APR 1258 LUT / 344 FF /
7 EBR / 30.254 MHz. Экономия 29/10 LUT, новых тактов нет. Cold FB + DIR
сохранил все прежние counts и UART: 354938300 clocks / 3984366 retirements
в production и 412130048 clocks / 3987390 retirements с APR. В обоих
300 RK commands, 3270 UART bytes, 162 SD reads / 6 writes.

CPU APR workload сохраняет 221988 clocks для 432 readbacks / 720 CSR beats;
helper по-прежнему добавляет 10 clocks/memory word. FIS exact corpus прошёл
на RAM/FRAM/vendor. Это simulation и TRACE; hardware MMU и RT-11XM ещё не
измерены. [Все fits и проверки CP40](area-datapath.md).

## CP39 — CSR без дополнительных lookup clocks

Experimental board: 1268 LUT / 344 FF / 7 EBR / 31.075 MHz. Прежний helper:
+10 clocks/memory word. CSR controller read/PDR write — 2 clocks, PAR write
с paired W clear — 3 clocks; CPU turnaround/release в эти latency не входят.
Directed CPU/SPI FRAM program: 432 readbacks, 720 CSR beats, 4902 lookup reads,
221988 clocks на portable и vendor EBR. Это тест CSR, а не типичный ISA CPI.

Cold RT-11FB + DIR сохранил CP38g counts и UART: 412130048 clocks,
3987390 retirements, 3397976 FRAM transactions, 300 RK commands, 663 timer
edges, 3270 UART bytes, 162 SD reads / 6 writes. Полная MMU/RT-11XM и
hardware throughput пока не измерены. [Evidence CP39](mmu-apr-csr.md).

## CP38 — прежние clocks при меньшей площади

Production CP38f: 1188 LUT / 326 FF / 6 EBR / 30.943 MHz; APR CP38g:
1228 LUT / 341 FF / 7 EBR / 32.470 MHz. Read mux не добавляет тактов.
Оба cold RT-11FB + DIR runs сохранили все прежние counters и UART:
production 354938300 clocks / 3984366 retirements, APR 412130048 clocks /
3987390 retirements. Соответственно 3390712 / 3397976 FRAM transactions,
576 / 663 timer edges; в обоих 300 RK commands, 3270 UART bytes и
162 SD reads / 6 writes. Сохранён прежний overhead APR: +10 clocks/memory word.

Это simulation workload и отдельный TRACE, не hardware throughput.
CPU CSR/translation/MMR/high DMA и RT-11XM пока не проверяются.
[Полные counts и сравнение вариантов](area-board-read.md).

## CP37 — latency APR lookup и cold RT-11FB

Вариант d выполняет 9 helper words и входной redirect: **10 extra clocks
на FETCH/READ/WRITE без hold**. EBR выдаёт данные на APR_READ edge;
следующая ALU-микрокоманда сохраняет их, отдельного wait не требуется.
Icarus без hold: 19710 extra clocks / 1971 entries. Полный CPU miter:
5512747 extra = 10×390550 + 1607247 held edges.

Cold FB + DIR с полной периферией: **412130048 clocks** против 406268404
у CP36g с context hook и 354938300 у production CP36f. 3987390 retirements,
5222610 read / 424452 write beats, 3397976 FRAM transactions, 300 RK commands,
663 timer edges, 3270 UART bytes, 162 SD reads / 6 writes. UART совпал;
таймерная работа меняется с длительностью, поэтому это не чистый CPI lookup.

Fit CP37d/e: 1265 LUT / 341 FF / 7 EBR / 30.327 MHz. Это simulation workload
и TRACE, не измерение на физической плате. CPU CSR/translation/MMR/high DMA
не включены; результат не является benchmark MMU или RT-11XM.
[Контракт и исходные отчёты](mmu-apr-lookup.md).

## CP36 — прежние cycles при меньшей площади

Production: 1222 LUT / 326 FF / 6 EBR / 31.116 MHz, −30 LUT от CP31c.
С context hook: 1243 LUT / 338 FF / 6 EBR / 31.107 MHz, −30 LUT от CP35e.
Новый opcode index и перенос byte mux не добавляют clock/EBR/FF.
Рабочая частота прежняя, 29.56 MHz; рост TRACE Fmax не объявляется speedup.

Все счётчики двух cold RT-11FB + DIR runs побайтно/численно совпали со
своими baselines: **354938300 clocks** без hook, **406268404** с ним.
В обоих 3270 UART bytes и 162 SD reads / 6 writes. CPU miter также сохранил
normal/entry/hold counts CP35 для 69632 случаев. FIS/FRAM/vendor проверки прошли.
Новый benchmark MMU/XM отсутствует. [Полные counts и границы](area-decode.md).

## CP35 — стоимость context hook и cold RT-11FB

В CPU miter дополнительное время точно равно `9 × entries + held edges`:
4759934 clocks = 9×390550+1244984. Helper упражняет сохранение PSW,
не выполняет MMU translation; это не итоговый CPI MMU. Сравниваемый
исходный CPU останавливает clock только на private edges кандидата.

Полный board CP35e с постоянно включённым hook и hold=0 прошёл cold
RT-11FB + DIR за **406268404 clocks**, против **354938300** у CP31c
(+14.46%). Получены 3986525 retirements, 5221066 read / 424227 write beats,
3395982 FRAM transactions, 300 RK commands, 654 timer edges,
3270 UART wire bytes, 162 SD reads / 6 writes. Таймер добавляет работу
при увеличении длительности, поэтому разность clock counts не следует
делить на исходное число memory accesses как стоимость одной routine.

Fit: 1273 LUT / 338 FF / 6 EBR / 30.498 MHz при 29.56 MHz PASS.
Это simulation workload и TRACE placement, не измерение физической платы.
Исходный образ неизменён; XM и high-memory CPU/DMA отсутствуют.
[Контракт, исходные логи и ограничения](mmu-entry.md).

## CP34 — сравнение площади ALU, без нового CPU benchmark

Разделение relocation с ALU даёт в одинаковом datapath probe 466 вместо
471 LUT и 41.315 вместо 42.535 MHz. Полное разделение relocation/length
требует 487 LUT, 39.156 MHz и отвергнуто. Probe включает измерительные
FF, не содержит CPU MMU sequencer и не даёт CPI/throughput с MMU.
Счётчики циклов при подмене T5–T7 относятся к regression fixtures,
а не к реализованной MMU routine. [Измерения и границы](mmu-sharing.md).

## CP33 — latency доступа к APR, без нового CPU benchmark

Изолированный APR controller возвращает ACK read/PDR-write/mark-W за
2 clocks, PAR-write с очисткой W — за 3 clocks; request release не включён.
CSR probe CP33c: 40 LUT / 65 FF / 1 EBR / 96.862 MHz, из FF только 3
относятся к controller. В serial translation test PAR/PDR сохраняются
testbench latches; синтезированной CPU MMU pipeline ещё нет. CPU CPI с MMU,
RK DMA >64 КиБ и RT-11XM benchmarks не измерены. [Контракт CP33](mmu-apr.md).

## CP32 — translation / SPI verification, без нового CPU benchmark

Translator 18/22 bits не включён в production CPU. Изолированный probe:
70 LUT, 80 измерительных FF, 0 EBR, 93.362 MHz. Этот Fmax не является
частотой исполнения PDP-11 с MMU. Full SPI test: 196634 beats,
24774246 clocks; сюда входят намеренные held requests, паузы и проверки
ошибок, поэтому это счётчики verification harness, не CPU CPI/throughput.
Проверено содержимое всех 128 КиБ FRAM model. CPU microclocks/instruction,
SD/RK DMA выше 64 КиБ и RT-11XM с MMU пока не измерены.
[Проверки и ограничения CP32](mmu.md), [manifest](verification-cp32.json).

## CP31 — FIS-only / подготовка MMU

FP11 удалён; FIS и integer microcode побайтно совпадают с CP29.
После переноса RK CSR в firmware EBR cold RT-11FB + DIR прошёл за
354938300 clocks (CP31a до переноса: 355132188), 3270 UART wire bytes,
162 SD reads / 6 writes. Число retirements изменилось из-за polling/IRQ;
это системный workload с UART pacing, не изолированный memory benchmark.
MMU probe ещё не включён в CPU; CPI с MMU и RT-11XM **не измерены**.
[Отчёты и границы сравнения](mmu.md).

## CP30 — первый FP control subset

При sync decode и RAM без ожиданий: LDFPS/STFPS Rn — 6 microclocks,
CFCC и SETF/I/D/L — 10; у каждой один внешний opcode fetch. Corpus из
90112 состояний с 0…3 wait states: 757760 clocks. Это ещё не FP arithmetic
benchmark. FP-enabled cold RT-11 + DIR: 355134893 clocks, 3983747 retirements,
3270 UART wire bytes; физическую прошивку CP29 не меняли.
[Методика и ограничения](fp11a.md).


## CP28 — полный cold RT-11 workload

Полный HC1200 top без prefetch, microstore 954×36 и ROM dispatch:
**355132188 clocks /3983731 retirements** от холодного reset до prompt после DIR.
Включены bootstrap, STARTF.COM, private RK firmware и ожидания устройств;
это не warm ALU loop. Получены 98 Files, 2201 Blocks и 51455 Free blocks.

* 5215901 read beats /423446 write beats, включая error responses;
* 3393830 FRAM CS assertions;
* 162 SD reads /6 writes в RAM overlay неизменённого backing image;
* 3270 проверенных UART wire bytes, 576 KW11 events.

Среднее 89.145625 microclocks/retirement. При nominal 29.56 MHz:
12.013944 s и 331592 retirements/s; это расчёт по RTL counters, не измерение FPGA.
В ideal RAM mode новый board decoder добавляет один FETCH clock: MOV/ADD/CMP/BR
занимают 3 clocks с 0 waits; default combinational core сохраняет 2 clocks.
Число memory beats не увеличилось. Предыдущие CP27 prefetch/FIS benchmarks
остаются историческими данными другого memory scope.
[Методика/границы](hc1200-integration.md), [машинные данные](verification-cp28.json),
[UART transcript](../tb/reports/cp28/cp28-uart.txt).

## CP27: FIS на RAM и SPI FRAM

24 новых workload runs на каждую ROM-модель; portable Verilator и Lattice
DP8KC/Icarus дают побайтно одинаковые результаты. В каждом workload 32
измеренных цикла `MOV/MOV/MOV/FIS/BR`, 160 инструкций и 576 demand memory
beats; первый разогревающий цикл исключён. Интервал FIS измерен от retirement
предыдущего MOV до retirement FIS, поэтому включает fetch и memory waits.

| Пример | A / B, F-format hex | FIS interval RAM | Sequential FRAM | FRAM+prefetch | Loop CPI, prefetch | FIS/s в loop @29.56 MHz |
|---|---|---:|---:|---:|---:|---:|
| FADD | 40800000 / 40800000 | 252 | 953 | 947 | 372.6 | 15,867 |
| FSUB | 40c00000 / 40800000 | 259 | 960 | 954 | 374.0 | 15,807 |
| FMUL | 40c00000 / 40c00000 | 449 | 1150 | 1144 | 412.0 | 14,350 |
| FDIV | 40800000 / 40c00000 | 599 | 1300 | 1294 | 442.0 | 13,376 |
| FADD, signed alignment | c0800001 / 34800001 | 404 | 1105 | 1099 | 403.0 | 14,670 |
| FSUB, cancellation | 40800001 / 40800000 | 391 | 1092 | 1086 | 400.4 | 14,765 |
| FMUL, minimum exponent | 00800000 / 41000000 | 453 | 1154 | 1148 | 412.8 | 14,322 |
| FDIV, negative | c0c00000 / 40a00000 | 596 | 1297 | 1291 | 441.4 | 13,394 |

Пропускная способность рассчитана из simulated clocks при штатных CPU/SPI
29.56/14.78 MHz; это не измерение на плате. Latency зависит от operands.
Каждый результат, flags и состояние проверяются, это не пустой timing loop.
1146 прежних integer benchmarks остаются историческими результатами CP26;
в CP27 они заново не прогонялись. Свежая integer-регрессия и формальное
доказательство сохранения datapath описаны в [FIS checkpoint](fis.md).

## CP26: DIV, все S modes и signed boundaries

1146 benchmarks/ROM: 1082 прежних и 64 новых. Все 120 прежних result files
побайтно равны CP25, все 128 portable/vendor results равны.
[Методика и исходные счётчики](eis-div.md), [JSON](benchmarks-cp26.json).

| S mode | RAM loop CPI | Legacy FRAM CPI | Sequential FRAM CPI | Prefetch CPI | DIV interval, prefetch | DIV/s в полном loop @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 52.217391 | 225.695652 | 114.826087 | 98.847826 | 157 | 97,515 |
| 1 | 54.173913 | 261.891304 | 173.195652 | 169.282609 | 268 | 56,941 |
| 2 | 53.702128 | 261.468085 | 171.765957 | 167.808511 | 269 | 56,219 |
| 3 | 54.340426 | 295.617021 | 205.914894 | 201.957447 | 376 | 46,713 |
| 4 | 53.702128 | 261.468085 | 171.765957 | 167.808511 | 269 | 56,219 |
| 5 | 54.021277 | 295.297872 | 205.595745 | 201.638298 | 375 | 46,787 |
| 6 | 54.826087 | 296.782609 | 185.913043 | 182.000000 | 307 | 52,962 |
| 7 | 55.478261 | 331.673913 | 220.804348 | 216.891304 | 414 | 44,442 |

Холодные ordinary RR (4806 cases, R0/R1, S=R2, без T/IRQ): от начала fetch
до retirement **15–149 тактов RAM**, **120–251 FRAM**. Эти интервалы
включают ранние выходы по нулю/переполнению и odd-R расширение. Они
не эквивалентны warmed loop interval и не означают постоянную latency DIV.


## CP25: MUL, все addressing modes и signed edge cases

**1082 benchmarks/ROM:** 1018 прежних и 64 новых. Все старые 68 benchmark
JSON и 44 cycle CSV побайтно равны CP24; все 120 portable/vendor results
совпали. CPI смешанного loop включает MOV/BR; отдельная MUL latency и
пропускная способность умножений приведены явно. [Методика](eis-mul.md).

Измерения portable и vendor ROM совпали во всех 64 новых workload runs.
CPI ниже — всего цикла; MUL interval включает fetch и memory waits.

| S mode | Ideal RAM loop CPI | Legacy FRAM CPI | Sequential FRAM CPI | FRAM+prefetch CPI | MUL interval, prefetch | MUL/s в полном loop @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 69.904762 | 226.571429 | 126.190476 | 105.031746 | 156.000000 | 138,486 |
| 1 | 72.857143 | 281.190476 | 214.269841 | 211.317460 | 267.000000 | 68,832 |
| 2 | 72.468750 | 280.828125 | 212.828125 | 209.828125 | 268.000000 | 68,237 |
| 3 | 73.437500 | 332.656250 | 264.656250 | 261.656250 | 375.000000 | 54,721 |
| 4 | 72.468750 | 280.828125 | 212.828125 | 209.828125 | 268.000000 | 68,237 |
| 5 | 72.953125 | 332.171875 | 264.171875 | 261.171875 | 374.000000 | 54,823 |
| 6 | 73.841270 | 333.841270 | 233.460317 | 230.507937 | 306.000000 | 63,102 |
| 7 | 74.825397 | 386.492063 | 286.111111 | 283.158730 | 413.000000 | 51,368 |

FRAM+prefetch счётчики за восемь измеряемых loops: всего 248 MUL на workload.

| S mode | Retirements | Microclocks | MUL interval clocks, сумма | Demand beats | SPI transactions | SPI clocks |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 504 | 52936 | 38688 | 752 | 8 | 12288 |
| 1 | 504 | 106504 | 66216 | 1000 | 504 | 32128 |
| 2 | 512 | 107432 | 66464 | 1016 | 504 | 32384 |
| 3 | 512 | 133968 | 93000 | 1264 | 752 | 44288 |
| 4 | 512 | 107432 | 66464 | 1016 | 504 | 32384 |
| 5 | 512 | 133720 | 92752 | 1264 | 752 | 44288 |
| 6 | 504 | 116176 | 75888 | 1248 | 504 | 36096 |
| 7 | 504 | 142712 | 102424 | 1496 | 752 | 48000 |

Восемь отдельных register edge workloads проверяют зависимость цикла от данных:

| R × S, hex | RAM MUL interval | FRAM+prefetch MUL interval | FRAM+prefetch loop CPI |
|---|---:|---:|---:|
| 0000 × 8000 | 111 | 142 | 98.142857 |
| 0001 × 7fff | 111 | 142 | 98.142857 |
| 0001 × 8000 | 114 | 145 | 99.619048 |
| 0002 × 4000 | 113 | 144 | 99.126984 |
| ffff × 8000 | 147 | 178 | 115.857143 |
| 8000 × 8000 | 116 | 147 | 100.603175 |
| ffff × ffff | 145 | 176 | 114.873016 |
| 7fff × 7fff | 140 | 171 | 112.412698 |

В отдельной differential suite ordinary RR (R0/R1, S=R2, без T/IRQ) latency
от начала fetch до retirement составила **109–150 тактов RAM**, **214–252 FRAM**.
Это холодный одиночный instruction test, другой интервал, чем warmed loop.
Длительность зависит от popcount битов R, знаков и формирования flags;
число итераций всегда 16. Полные JSON содержат и счётчики всех старых workloads.
Все 1018 прежних benchmarks побайтно сохранены. AM4 MUL был источником
sequencing, но сопоставимого измеренного AM4 MUL CPI/Fmax в материалах нет;
численного ускорения относительно AM4 здесь не заявляется.

Core/FRAM: **841/1089 LUT**, **299/416 FF**, **4 EBR**.
35/29.56 MHz PASS, TRACE 36.926/30.672 MHz. Это simulation, не board measurement.

## CP24: XOR во всех destination modes

**1018 benchmarks на ROM-модель:** 986 прежних и 32 новых XOR workloads.
Все 64 старых benchmark JSON и 40 instruction/fault cycle CSV побайтно равны
CP23. Между portable и vendor совпали все 68 benchmark JSON и 44 cycle CSV.
XOR register loop на ideal RAM — 512 clocks / 256 retirements = **2 CPI**.
Память системы — SPI FRAM; её значения приведены отдельно ниже.

При CPU/SPI **29.56/14.78 MHz** register loop на FRAM с prefetch:
**10280 clocks / 256 retirements = 40.156250 CPI**,
расчётно **736,125 instructions/sec**. Это функциональная
симуляция реального SPI transport, не измерение физической платы.

| Destination mode | Ideal RAM CPI | Legacy FRAM CPI | Sequential FRAM CPI | FRAM + prefetch CPI | Demand beats/insn, prefetch | Расчётные insn/sec @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 2.000000 | 107.000000 | 41.125000 | 40.156250 | 1.000000 | 736,125 |
| 1 | 9.750000 | 334.656250 | 334.656250 | 334.656250 | 2.937500 | 88,329 |
| 2 | 10.909091 | 332.333333 | 328.212121 | 328.030303 | 2.909091 | 90,114 |
| 3 | 12.787879 | 432.848485 | 428.727273 | 428.545455 | 3.848485 | 68,978 |
| 4 | 10.909091 | 332.333333 | 328.212121 | 328.030303 | 2.909091 | 90,114 |
| 5 | 11.848485 | 431.909091 | 427.787879 | 427.606061 | 3.848485 | 69,129 |
| 6 | 11.687500 | 438.312500 | 372.437500 | 372.437500 | 3.906250 | 79,369 |
| 7 | 13.625000 | 541.968750 | 476.093750 | 476.093750 | 4.875000 | 62,089 |

Полные счётчики FRAM + prefetch за восемь измеряемых loops:

| Destination mode | Retirements | Microclocks | Demand beats | SPI transactions | SPI clocks |
|---|---:|---:|---:|---:|---:|
| 0 | 256 | 10280 | 256 | 8 | 4352 |
| 1 | 256 | 85672 | 752 | 1000 | 38080 |
| 2 | 264 | 86600 | 768 | 1000 | 38336 |
| 3 | 264 | 113136 | 1016 | 1248 | 50240 |
| 4 | 264 | 86600 | 768 | 1000 | 38336 |
| 5 | 264 | 112888 | 1016 | 1248 | 50240 |
| 6 | 256 | 95344 | 1000 | 1000 | 42048 |
| 7 | 256 | 121880 | 1248 | 1248 | 53952 |

Все 32 новых и 986 прежних benchmarks прошли с portable и vendor EBR.
Старые значения тактов, memory beats и SPI activity не изменились.
[Исходные JSON и производные CPI/IPS](benchmarks-cp24.json).

Core/FRAM area: **854/1089 LUT**, FF **299/416**, **4 EBR**.
Constraints 35/29.56 MHz PASS; TRACE Fmax 36.059/30.273 MHz.
Все значения CPI получены функциональной симуляцией; board measurement
и timing внешних выводов не заявлены. [Полный XOR отчёт](eis-xor.md).

## CP23: площадь без изменения cycle counts

Все **986 benchmarks на ROM-модель** повторены. 64 benchmark JSON и 40
instruction/fault cycle CSV побайтно равны CP22 на portable/vendor ROM;
20 C fixture files тоже прежние. Это включает все microclocks, memory beats,
SPI transactions и SPI clocks. Таблицы CPI CP22 ниже остаются действующими.

Core/FRAM area: 850/1087 LUT вместо 856/1098. При прежних CPU/SPI
29.56/14.78 MHz инструкции выполняются за то же время. TRACE Fmax FRAM
30.193 MHz вместо 30.457; сравнение при максимальных разных частотах здесь
не подменяет измерение при общем clock constraint.

[Полные свежие результаты](benchmarks-cp23.json), [manifest](verification-cp23.json),
[три synthesis-варианта](area-sequencer.md). Физическая плата не измерялась.

## CP22: ASHC и исправленный ASH

986 runs на ROM-модель. Из 890 прежних benchmark runs 871 сохраняет counts;
19 ASH/prefetch workloads изменили timing после исправления порядка EA.
Новые 24 workloads × 4 memory modes: register/immediate/memory count,
значения 0, +1, +15, +16, +31, −32, −31, −1. Один warmup loop исключён,
затем измерены 256 retirements смеси 31 ASHC + BR. Это CPI смеси,
а не latency отдельной ASHC.

| Count в регистре | Ideal RAM CPI | Legacy FRAM | Sequential FRAM | Prefetch FRAM |
|---|---:|---:|---:|---:|
| 0 | 20.40625 | 125.40625 | 59.53125 | 42.09375 |
| 1 | 28.15625 | 133.15625 | 67.28125 | 42.09375 |
| 15 | 95.96875 | 200.96875 | 135.09375 | 99.25 |
| 16 | 100.8125 | 205.8125 | 139.9375 | 104.09375 |
| 31 | 173.46875 | 278.46875 | 212.59375 | 176.75 |
| -32 | 179.28125 | 284.28125 | 218.40625 | 182.5625 |
| -31 | 174.4375 | 279.4375 | 213.5625 | 177.71875 |
| -1 | 29.125 | 134.125 | 68.25 | 42.09375 |

Отдельная ASHC с ideal FETCH: 21 clocks при count=0, 24+5n при left shift,
25+5n при right shift. Register loops дают 1 demand beat/instruction;
immediate/memory — 1.96875 с учётом BR. CPU/SPI nominal 29.56/14.78 MHz.
Это функциональная RTL simulation с настоящей моделью SPI FRAM и двумя
ROM models; физическая плата не измерялась.

### Изменения прежнего ASH при MEMORY_MODE=2

CALL с prefetch=0 теперь предшествует ALU snapshot; это меняет окно запуска
speculative reads даже без перекрытия регистров. Во всех трёх остальных
memory modes ASH benchmark counts сохранены. Все 794 non-ASH benchmarks
также неизменны. SPI transactions и demand beats сохранены во всех 890 runs.

| ASH workload | Runs | Изменение CPI смеси | Изменение SPI clocks/instruction |
|---|---:|---:|---:|
| Register, count 0/+1/−1 | 3 | +1.9375 | 0 |
| Immediate, все 8 counts | 8 | −29.0625 | −15.5 |
| Memory, все 8 counts | 8 | −30.03125 | −15.5 |

Короткий register loop: 40.15625 → 42.09375 CPI. Остальные 5 длинных register
loops не изменились. Правильный порядок EA сохранён; запуск prefetch требует
отдельной оптимизации и A/B fit. Полные before/after records включены в JSON.

1024 ASH fault fixtures и все non-cycle CSV fields прежние. В RAM 408 ранних
EA faults пропускают snapshot и завершаются на один clock раньше:
35180 → 34772 clocks суммарно. FRAM timing изменился во всех 1024 случаях:
651680 → 640308 clocks; архитектурные bus beats по-прежнему 6280.

[Полные данные](benchmarks-cp22.json), [алгоритм и проверки](eis-ashc.md),
[manifest](verification-cp22.json).


## Исторический CP21: ASH

890 runs/ROM; все794 прежних counts неизменны. Новые24 workloads×4 memory
modes: register/immediate/memory count, значения0,+1,+15,+16,+31,−32,−31,−1.
Один warmup loop исключён, затем256 retirements смеси31 ASH+BR.

| Count в регистре | Ideal RAM CPI | Legacy FRAM | Sequential FRAM | Prefetch FRAM |
|---|---:|---:|---:|---:|
| 0 | 9.75 | 114.75 | 48.875 | 40.15625 |
| 1 | 19.4375 | 124.4375 | 58.5625 | 40.15625 |
| 15 | 73.6875 | 178.6875 | 112.8125 | 76.96875 |
| 16 | 77.5625 | 182.5625 | 116.6875 | 80.84375 |
| 31 | 135.6875 | 240.6875 | 174.8125 | 138.96875 |
| -32 | 105.65625 | 210.65625 | 144.78125 | 108.9375 |
| -31 | 102.75 | 207.75 | 141.875 | 106.03125 |
| -1 | 15.5625 | 120.5625 | 54.6875 | 40.15625 |

Отдельная ASH с ideal FETCH:10 clocks при0,16+4n при left shift и13+3n при right
shift. Это latency, не CPI loop. Register loops дают1 demand beat/instruction;
immediate/memory —1.96875 с учётом BR. FRAM prefetch не скрывает длинные serial shifts.
[Полные данные](benchmarks-cp21.json), [algorithm и ограничения](eis-ash.md).
CPU/SPI nominal 29.56/14.78 MHz; физическая плата не измерялась.


## CP20: HALT restart / RESET

794 runs/ROM, все786 прежних counts сохранены. Core836/299/4 при35 MHz PASS,
FRAM+resolver1085/416/4 при29.56 MHz PASS. Новые loops:

| Workload | Ideal RAM CPI | Legacy FRAM | Sequential FRAM | Prefetch FRAM | Bus beats/instruction |
|---|---:|---:|---:|---:|---:|
| 31 RESET + BR | 3.9375 | 108.9375 | 43.0625 | 43.0625 | 1 |
| HALT; RTI; BR | 7.333333 | 298.666667 | 298.666667 | 298.666667 | 2.666667 |

RESET alone с ideal FETCH —4 clocks, HALT —12. Warmup исключён:256/192
измеренных retirements. При CPU/SPI29.56/14.78 MHz FRAM loops дают
рассчитанные~0.686/~0.099 MIPS. Это RTL simulation, не board measurement.
6144 новых C cases:137216 RAM/3055232 FRAM clocks,26368 exact bus beats;
нет новых exclusions. [Данные](benchmarks-cp20.json),
[HALT profile и peripheral RESET](system-control.md).


## CP19: MFPS/MTPS

786 runs на ROM-модель; все 766 прежних counts сохранены. Core844/297/4,
35 MHz PASS/36.426; FRAM+resolver1073/414/4,29.56 MHz PASS/30.046.

| Workload | Ideal RAM CPI | Legacy FRAM CPI | Sequential FRAM CPI | Prefetch FRAM CPI | Bus beats/instruction |
|---|---:|---:|---:|---:|---:|
| MFPS R0 | 2 | 107 | 41.125 | 40.15625 | 1 |
| MTPS R0 | 7.8125 | 112.8125 | 46.9375 | 40.15625 | 1 |
| MFPS (R1) | 9.75 | 216.46875 | 216.46875 | 216.46875 | 1.96875 |
| MTPS (R1) | 13.625 | 203.875 | 203.875 | 203.875 | 1.96875 |
| MTPS #value | 14.59375 | 204.84375 | 204.84375 | 204.84375 | 1.96875 |

31 instructions и BR,8 measured loops после одного warmup. У MTPS immediate
каждая инструкция содержит extension word. Отдельная MFPS Rn занимает 2 clocks
с ideal FETCH, MTPS Rn — 8; loop CPI включает более быстрый BR.
Prefetch скрывает дополнительную ALU работу MTPS в register stream; при
nominal 29.56 MHz получается~0.736 M instructions/s. В immediate варианте READ
из T1 пока относится к data, а не stream. Memory-forms ограничены SPI.

Новая differential группа:12852 completed cases,54492 bus beats,
355470 RAM/6370712 FRAM clocks;204 abort/I/O/stack candidates исключены явно.
Отдельно 1008 fault frames:33840/615848 clocks,6336 beats,112 repair clocks.
Failed instructions не считаются retire/IPS. Portable/vendor counts совпали.
[Данные](benchmarks-cp19.json), [границы профиля и microcode](psw-transfer.md).


## CP18: CC/NOP/MFPT

766 runs на ROM-модель; все746 прежних counts сохранены. Final core843/297/4,
35 MHz PASS/35.674; FRAM+resolver1062/414/4,29.56 MHz PASS/30.409.
Новые measured loops используют RAM и три FRAM policies:

| Workload | Ideal RAM CPI | Legacy FRAM CPI | Sequential FRAM CPI | Prefetch FRAM CPI | Bus beats/instruction |
|---|---:|---:|---:|---:|---:|
| NOP_clear | 3.9375 | 108.9375 | 43.0625 | 40.15625 | 1 |
| NOP_set | 3.9375 | 108.9375 | 43.0625 | 40.15625 | 1 |
| CC_all_masks | 3.93939394 | 108.939394 | 43 | 40.0909091 | 1 |
| MFPT | 2 | 107 | 41.125 | 40.15625 | 1 |
| CC_branch_dependencies | 3.2 | 108.2 | 81 | 79.8 | 1 |

CC/NOP отдельной instruction —4 clocks с ideal FETCH, MFPT —2.
NOP/MFPT loops содержат31 одинаковую instruction и BR; CC_all_masks —32
CC encodings и BR. Измерены8 полных loops после одного warmup loop.
CC_branch_dependencies — SEC; taken BCS; CLC; taken BCC; BR,64 loops после
warmup; обе branches пропускают reserved instruction, что проверяет новые flags.
PSW/R0..R7 и отсутствие неожиданных control effects проверяются в benchmark.

Все loops имеют один logical bus beat на instruction, но разные SPI CS/restart
counts. Поэтому MFPT40.15625 CPI здесь нельзя напрямую сравнивать с39.078125
старого RR loop другой длины. При nominal 29.56 MHz новые NOP/MFPT streams
дают расчётные~0.736 M instructions/s, CC_mask~0.737 M; это RTL simulation,
не результат платы. Full counts и calculated IPS — [benchmarks-cp18.json](benchmarks-cp18.json).
[Microcode и ограничения](system-flags.md).

## CP17: trace/RTT

746 runs на ROM-модель; все 726 прежних counts сохранены. Пять новых
workloads проверены с RAM и тремя FRAM policies. Core 826/297/4,
35 MHz PASS/36.302; FRAM+resolver 1046/414/4, 29.56 MHz PASS/30.194.

| Workload | Ideal RAM CPI | FRAM/prefetch CPI | Bus beats/instruction |
|---|---:|---:|---:|
| ADD_trace_RTT | 13 | 458 | 4 |
| MOV_mem_trace_RTT | 16.25 | 479.5 | 4.25 |
| RTI_restored_T_loop | 24 | 793 | 7 |
| WAIT_trace_RTT | 13.25 | 450.25 | 4 |
| MOV_SP_RTT_T0 | 12.5 | 204 | 2.5 |

В первых, втором и четвёртом loops участвует BR; trace frame не считается
инструкцией, но его clocks включены. Это simulation при nominal CPU/SPI
29.56/14.78 MHz, не измерение на плате. [Данные](benchmarks-cp17.json),
[semantics и полный состав loops](trace-rtt.md).


## CP16: успешные loops сохранены, ошибки измерены отдельно

Все 726 прежних benchmark runs на каждую ROM-модель и десять ordinary
per-case cycle CSV совпали с CP15. RR остаётся 2 CPI ideal RAM и 39.078125 CPI
в прежнем word FRAM/prefetch loop. Эти цифры относятся к успешным инструкциям.

| Fault-frame fixture | Cases | Microclocks | Logical bus beats | Repair clocks |
|---|---:|---:|---:|---:|
| RAM, waits=case_id%4 | 32780 | 1179328 | 213080 | 3536 |
| SPI FRAM + logical error-injection slave | 32780 | 20896206 | 213080 | 3536 |

Это время от начала failed instruction/FETCH до завершения vector frame,
включая EA и ожидание памяти. Failed instructions и frames не порождают retire;
называть эти counts PDP-11 instructions/sec нельзя. Четыре frame beats сами
занимают 16 microclocks от fault edge при zero-wait RAM, repair добавляет один.
Odd word не создаёт failed external beat; ACK error создаёт ровно один.

Clock scope: CP16e core 809/296/4, 35 MHz PASS/36.552; CP16f FRAM+resolver
1042/413/4, 29.56 MHz PASS/31.117. [Данные](benchmarks-cp16.json), [fault profile](memory-faults.md).

## CP15: reserved/invalid-mode traps

726 portable/vendor runs, все 714 CP14 counts сохранены. Новые loops:
trap, RTI, BR; 3 warmup, 384 measured instructions. Frame clocks включены
в CPI; отдельной инструкцией frame не считается. Core778/293/4,
35 PASS/36.236; FRAM+IRQ resolver 1036/410/4, 29.56 PASS/31.697 MHz.

| Workload | Ideal RAM CPI | Legacy FRAM | Sequential | Prefetch | Beats/instr | Calculated instr/s @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| reserved_RTI | 9 | 335.333333 | 335.333333 | 346.333333 | 3.000000 | 85,351 |
| invalid_JMP_RTI | 9 | 335.333333 | 335.333333 | 346.333333 | 3.000000 | 85,351 |
| invalid_JSR_RTI | 9 | 335.333333 | 335.333333 | 346.333333 | 3.000000 | 85,351 |

Один reserved/invalid-mode trap занимает 17 clocks с ideal RAM. Это не
измерение отсутствующих EIS/system instructions, которые также получают
fallback vector010. [Все 12 новых runs](benchmarks-cp15.json),
[точная область совместимости](reserved-traps.md).

## CP14: IRQ, WAIT и SPL

714 portable/vendor runs; все прежние 690 counts сохранены. Core 764/293/4,
35 MHz PASS/36.720; FRAM+KW11/KL11 IRQ adapter 1016/410/4,
29.56 MHz PASS/31.221. Полная периферия платы не входит в fit.

CPI относится к полному loop, включая обработчик и BR. SPL loop: восемь
SPL и BR, 9 warmup/288 measured instructions. Обычные IRQ loops: instruction,
RTI, BR, 3/192; nested loop: ADD, RTI, RTI, BR, 4/256. IRQ frame не считается
инструкцией, но все его clocks и memory/SPI transfers включены.

| Workload | Ideal RAM CPI | Legacy FRAM | Sequential | Prefetch | Beats/instr | Calculated instr/s @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| SPL_levels | 7.333333 | 112.333333 | 51.888889 | 45.666667 | 1.000000 | 647,299 |
| WAIT_IRQ_RTI | 9.333333 | 335.666667 | 335.666667 | 335.666667 | 3.000000 | 88,064 |
| ADD_IRQ_RTI | 9.333333 | 335.666667 | 335.666667 | 346.333333 | 3.000000 | 85,351 |
| MOV_mem_IRQ_RTI | 13.666667 | 375 | 375 | 375 | 3.333333 | 78,827 |
| MOV_store_IRQ_RTI | 14.333333 | 381.333333 | 381.333333 | 381.333333 | 3.333333 | 77,517 |
| nested_IRQ_RTI | 13 | 450 | 450 | 458 | 4.000000 | 64,541 |

IRQ frame через общую CP13 routine занимает 16 microclocks при ideal RAM;
SPL — 8 CPI; WAIT даёт один retire до ожидания и прекращает FETCH.
Повторный более высокий IRQ и два RTI проверяют сохранение PC/SP/PSW.
Небольшая цена prefetch на некоторых IRQ loops видна в counts, она не скрыта:
IRQ может прийти, когда следующее stream word уже читается.
[24 новых runs](benchmarks-cp14.json), [IRQ profile/tests](interrupts.md).

## CP13: software traps/RTI, kernel/T=0

Core+probe **735/277/4**, 35 MHz PASS, TRACE 36.340; FRAM/prefetch+probe
**999/394/4**, 29.56 MHz PASS, TRACE 31.245 MHz. 690 portable/vendor runs,
все прежние 666 counts неизменны. CPU/SPI nominal 29.56/14.78 MHz.
Это functional RTL simulation, не физическое измерение на плате.

Первые четыре loops: trap, RTI, BR; 3 warmup, 384 measured instructions.
Nested loop: BPT, IOT, RTI, RTI, BR; 5 warmup, 320 measured.
Последний: MOV #6000, SP и RTI; 2 warmup, 256 measured. CPI относится
к целому loop, а не только к названию trap/RTI.

| Workload | Ideal RAM CPI | Legacy FRAM | Sequential | Prefetch | Beats/instr | Calculated instr/s @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| BPT_RTI | 9 | 335.333333 | 335.333333 | 346.333333 | 3.000000 | 85,351 |
| IOT_RTI | 9 | 335.333333 | 335.333333 | 346.333333 | 3.000000 | 85,351 |
| EMT_377_RTI | 9 | 335.333333 | 335.333333 | 346.333333 | 3.000000 | 85,351 |
| TRAP_377_RTI | 9 | 335.333333 | 335.333333 | 346.333333 | 3.000000 | 85,351 |
| nested_BPT_IOT_RTI | 10.4 | 381 | 381 | 394.2 | 3.400000 | 74,987 |
| MOV_SP_RTI | 12.5 | 275 | 207 | 204 | 2.500000 | 144,902 |

Одно успешное BPT/IOT/EMT/TRAP с ideal RAM: 17 clocks; RTI: 8, включая
FETCH. Первый gate ориентирован на проверяемый frame при небольшом приросте
LUT, без отдельного trap FSM. Базовый RR path и весь CP12 workload сохранили
clocks/bus/SPI counts. [Все 24 новых runs](benchmarks-cp13.json),
[semantics/profile](software-traps.md), [verification](verification-cp13.json).
IRQ, trace и mode exchange не входят в эти измерения.

## CP12: SWAB/SXT/MARK

Final CP12c/d: core+probe **708/277/4**, 35 MHz PASS, TRACE 35.723;
FRAM/prefetch+probe **984/394/4**, 29.56 MHz PASS, TRACE 31.287 MHz.
Семь новых workloads × четыре memory modes. SWAB/SXT: 31 operation + BR,
32 warmup, 256 measured instructions. SXT N=0/N=1 измеряются отдельно.
MARK_inline_pop содержит MOV #continuation, R5; MARK 1; MOV #6000, SP; BR;
4 warmup, 256 measured instructions. Его CPI относится ко всему loop.

| Workload | Ideal RAM CPI | Legacy FRAM | Sequential | Prefetch | Beats/instr | Calculated instr/s @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| SWAB_reg | 19.4375 | 124.4375 | 58.5625 | 40.15625 | 1.000000 | 736,125 |
| SWAB_mem | 24.28125 | 349.1875 | 349.1875 | 349.1875 | 2.937500 | 84,654 |
| SXT_reg_positive | 2.96875 | 107.96875 | 42.09375 | 42.09375 | 1.000000 | 702,242 |
| SXT_reg_negative | 2.96875 | 107.96875 | 42.09375 | 42.09375 | 1.000000 | 702,242 |
| SXT_mem_positive | 10.71875 | 233.90625 | 233.90625 | 233.90625 | 1.968750 | 126,375 |
| SXT_mem_negative | 10.71875 | 233.90625 | 233.90625 | 233.90625 | 1.968750 | 126,375 |
| MARK_inline_pop | 10.5 | 194.25 | 126.25 | 132.25 | 1.750000 | 223,516 |

SWAB register сам по себе — 20 clocks, SXT register — 3 при ideal RAM,
включая FETCH. CPI в таблице учитывает BR. SWAB использует восемь RF/Q
shifts без дополнительного ALU hardware; его FRAM loop CPI 40.15625.
SXT register с текущей CJUMP prefetch policy имеет 42.09375 CPI, поэтому
малое число execution clocks само по себе не определяет SPI throughput.

Все counts — RTL simulation с FRAM transport/model, CPU/SPI nominal
29.56/14.78 MHz. Calculated instr/s не являются измерением на плате.
[28 новых runs](benchmarks-cp12.json), [microcode](extra-instructions.md),
[verification](verification-cp12.json). Прежние 638 runs проверяются recorder
на неизменность clocks/bus/SPI counts; portable/vendor JSON должны совпадать.

## CP11: control flow, stack и небольшая программа

Final CP11e/f: core+probe **694/277/4**, 35 MHz PASS, TRACE 36.358;
FRAM/prefetch+probe **970/394/4**, 29.56 MHz PASS, TRACE 31.672 MHz.
638 runs на каждой portable/vendor ROM, прежние 606 counts сохранены.
Nominal CPU/SPI — 29.56/14.78 MHz; counts измерены RTL simulation,
instr/s вычислены из counts, не измерены физической платой.

Все loops имеют warmup и целое число измеряемых periods. JMP self-loop:
256 instructions, JSR/RTS/BR: 384, nested calls: 320, MOV/SOB/BR: 576,
31 push + 31 pop + BR: 504. Программа sum1..16 вызывает ADD/SOB subroutine
через JSR PC и возвращает 136 через RTS PC; 296 measured instructions.

| Workload | Ideal RAM CPI | Legacy FRAM | Sequential | Prefetch | Beats/instr | Calculated instr/s @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| JMP_indirect_self | 7 | 112 | 112 | 112 | 1.000000 | 263,929 |
| JMP_absolute_self | 10 | 220 | 152 | 152 | 2.000000 | 194,474 |
| JSR_R5_RTS | 5.666667 | 186.333333 | 186.333333 | 186.333333 | 1.666667 | 158,640 |
| JSR_PC_absolute_RTS | 6.666667 | 222.333333 | 199.666667 | 199.666667 | 2.000000 | 148,047 |
| nested_JSR_R5_R4 | 6.4 | 202.2 | 202.2 | 202.2 | 1.800000 | 146,192 |
| SOB_countdown_16 | 3.666667 | 114.5 | 103.166667 | 102.833333 | 1.055556 | 287,455 |
| stack_31_push_31_pop | 17.253968 | 233.952381 | 233.952381 | 233.952381 | 1.984127 | 126,350 |
| program_sum_1_to_16 | 3.189189 | 120 | 81.405405 | 80.783784 | 1.108108 | 365,915 |

SOB itself с ideal RAM — 2 CPI при zero result, 3 при taken branch. Baseline
с PSW save/restore — 8/9 CPI. Все 4480 SOB differential cases ускорились
ровно на 6 clocks, остальные control cases сохранили counts. Программа
sum1..16 с prefetch: 28224→23912 clocks на 296 instructions;
countdown16: 76480→59232 на 576. Изменения FRAM clocks включают policy
speculation и SPI costs, поэтому не равны просто шести clocks на SOB.

[Все 32 control runs, baseline comparison и per-case totals](benchmarks-cp11.json),
[verification](verification-cp11.json), [ISA semantics](control-flow.md).
`make benchmark-control` / `make vendor-control-benchmark` воспроизводят
новые workloads; `tools/record_cp11.py` проверяет JSON/CSV и synthesis inputs.

## CP10: byte ISA

Final CP10j/k: core+probe **659/277/4**, 35 MHz PASS, Fmax 36.496;
FRAM/prefetch+probe **905/394/4**, 29.56 MHz PASS, Fmax 32.047 MHz.
CPU/SPI nominal 29.56/14.78 MHz. Counts — RTL simulation с настоящим
FRAM protocol; instr/s вычислены, физическая плата не программировалась.

606 runs на каждой portable/vendor ROM совпадают: прежние 430 counts
сохранены, добавлены 20 double-byte и 24 unary-byte workloads × 4 memory
modes. Loops: 31 operation + BR, 32 warmup, 256 measured retirements.
У byte unary R1/[R1]=8001 hex, поэтому low byte начинается с 01.

| Workload | Ideal RAM CPI | Legacy FRAM | Sequential | Prefetch | Beats/instr | Calculated instr/s @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| MOV_mem_reg_byte | 14.59375 | 204.84375 | 204.84375 | 204.84375 | 1.96875 | 144,305 |
| MOV_reg_mem_byte | 16.53125 | 223.25 | 223.25 | 223.25 | 1.96875 | 132,408 |
| MOV_mem_mem_byte | 18.46875 | 310.4375 | 310.4375 | 310.4375 | 2.9375 | 95,220 |
| CMP_mem_mem_byte | 19.4375 | 294.9375 | 294.9375 | 294.9375 | 2.9375 | 100,225 |
| MOV_immediate_reg_byte | 16.53125 | 206.78125 | 91.5 | 85.6875 | 1.96875 | 344,974 |
| CLRB_reg | 2 | 107 | 41.125 | 40.15625 | 1 | 736,125 |
| ADCB_reg | 2 | 107 | 41.125 | 40.15625 | 1 | 736,125 |
| CLRB_mem | 9.75 | 216.46875 | 216.46875 | 216.46875 | 1.96875 | 136,556 |
| COMB_mem | 10.71875 | 302.6875 | 302.6875 | 302.6875 | 2.9375 | 97,658 |
| INCB_mem | 11.6875 | 303.65625 | 303.65625 | 303.65625 | 2.9375 | 97,347 |
| TSTB_mem | 8.78125 | 199.03125 | 199.03125 | 199.03125 | 1.96875 | 148,519 |

Byte memory передаёт один data byte; opcode/pointer/extension остаются word.
Byte immediate сохраняет stream READ и потребляет полное внутреннее слово.
Различия CPI включают addressing microcode и SPI transfers. Зарегистрированный
PC redirect не изменил старые word cycle counts. [Все 176 byte runs](benchmarks-cp10.json),
[verification](verification-cp10.json). 2 CPI register ALU означает 17.5 млн
instr/s при прошедших 35 MHz и ideal RAM; это не скорость SPI FRAM.

Воспроизведение: `make benchmark benchmark-fram benchmark-ea benchmark-cp9 benchmark-byte`
и соответствующие vendor targets. Recorder `tools/record_cp10.py` требует
совпадения JSON/CSV, всех старых 430 counts и финальных synthesis input hashes.

## CP9: word unary и все branches

Final CP9e/f: core+probe **615/277/4**, 40 MHz PASS, Fmax 40.925;
core+FRAM/prefetch+probe **910/393/4**, 29.56 MHz PASS, Fmax 30.876.
CPU/SPI nominal 29.56/14.78 MHz. Counts ниже получены RTL simulation,
не физической платой; instr/s вычисляется как 29560000/CPI.

Unary loops: 31 operation + BR назад, 32 warmup, 256 measured instructions.
Register R1=8001 hex; indirect memory R1=4000, [4000]=8001; initial PSW=1.
По сравнению с прежним RR benchmark здесь один BR на 32 инструкции вместо
одного на 64, поэтому FRAM CPI регистра 40.15625 вместо 39.078125.

Branches: 31 одинаковых outcome + BR, 32 warmup, 256 measured.
Для taken-forward offset=+1, пропускаемое слово содержит STOP; not-taken
ветвь идёт к следующей инструкции. Self-loop offset=−1: 1 warmup, 256 measured.
Flags выбираются как первая NZVC комбинация 0..15, дающая нужный outcome.
Countdown: `MOV #16, R1; DEC R1; BNE DEC; BR start`, 34 warmup,
272 measured (8 полных циклов), последнее значение R1=0.

| Workload | Ideal RAM CPI | Legacy FRAM | Sequential | Prefetch | Beats/instr | Calculated instr/s @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| CLR_reg | 2 | 107 | 41.125 | 40.15625 | 1 | 736,125 |
| ADC_reg | 2 | 107 | 41.125 | 40.15625 | 1 | 736,125 |
| SBC_reg | 2 | 107 | 41.125 | 40.15625 | 1 | 736,125 |
| CLR_mem | 9.75 | 232.9375 | 232.9375 | 232.9375 | 1.96875 | 126,901 |
| COM_mem | 10.71875 | 335.625 | 335.625 | 335.625 | 2.9375 | 88,074 |
| INC_mem | 11.6875 | 336.59375 | 336.59375 | 336.59375 | 2.9375 | 87,821 |
| TST_mem | 8.78125 | 215.5 | 215.5 | 215.5 | 1.96875 | 137,169 |
| branch_2_not_taken | 2.96875 | 107.96875 | 42.09375 | 42.09375 | 1 | 702,242 |
| branch_2_taken_forward | 2.96875 | 107.96875 | 107.96875 | 107.96875 | 1 | 273,783 |
| branch_4_not_taken | 3.9375 | 108.9375 | 43.0625 | 43.0625 | 1 | 686,444 |
| branch_4_taken_forward | 3.9375 | 108.9375 | 108.9375 | 108.9375 | 1 | 271,348 |
| branch_6_not_taken | 4.90625 | 109.90625 | 44.03125 | 44.03125 | 1 | 671,341 |
| branch_6_taken_forward | 4.90625 | 109.90625 | 109.90625 | 109.90625 | 1 | 268,956 |
| branch_1_taken_self | 2 | 107 | 107 | 107 | 1 | 276,262 |
| branch_2_taken_self | 3 | 108 | 108 | 108 | 1 | 273,704 |
| countdown_MOV_16_DEC_BNE_BR | 2.91176471 | 111 | 73 | 72.35294118 | 1.02941176 | 408,553 |

Branch indices: 1 BR, 2 BNE, 3 BEQ, 4 BGE, 5 BLT, 6 BGT, 7 BLE,
8 BPL, 9 BMI, 10 BHI, 11 BLOS, 12 BVC, 13 BVS, 14 BCC, 15 BCS.
Полные 69 workloads × 4 memory modes: [benchmarks-cp9.json](benchmarks-cp9.json).
Все 430 runs на каждой portable/vendor ROM совпадают, включая 154 прежних
CP8 runs без изменения clocks, memory beats и SPI counts.

### Измеренный tradeoff branch prefetch

Первый CP9c/d fit проходил timing, но prefetch на branch predicate начинал
ненужный READ. Его нельзя оборвать посреди слова. Поэтому taken BNE self-loop
занимал 144 CPI вместо 108 sequential-only. Микрокодная пауза speculation
на CJUMP вернула 108 CPI без роста LUT/FF/EBR (повторный gate CP9e/f).
Not-taken BNE loop стал 42.09375 вместо 40.15625 CPI; у BGE 43.0625 вместо
40.15625, BGT 44.03125 вместо 40.15625. Это осознанная текущая цена паузы.
Countdown улучшился с 88.176471 до 72.352941 CPI; sequential-only — 73.

Raw unrestricted-policy benchmark JSON/log и первоначальные differential
logs сохранены в `tb/reports/cp9-prefetch-unrestricted/`. Сравнение по каждой
строке находится в `prefetch_policy_comparison` итогового JSON.

Отдельные instruction fixtures (не stream CPI): unary 7080 cases,
99242 RAM-wait clocks / 2035650 FRAM clocks / 19588 demand beats;
branch 7440 cases, 35464 / 805504 clocks / 7440 beats. Каждый fixture
начинается с холодного fetch. Старые EA и RR totals не изменились.

Воспроизведение: `make benchmark benchmark-fram benchmark-ea benchmark-cp9`
и четыре соответствующих `vendor-*` targets. Перед vendor run копировать
JSON/CSV из build с суффиксом `-portable`. `tools/record_cp9.py` требует
совпадения обоих наборов, сверяет CP8 baseline и provenance synthesis.


## Исторический CP8: семь word double-operand instructions

CP8d core+FRAM+probe: **878 LUT4 / 393 FF / 4 EBR**, 29.56 MHz PASS,
Fmax 31.540 MHz. CPU/SPI nominal 29.56/14.78 MHz. Все IPS ниже вычислены
из RTL clocks; физическая плата не программировалась.

Все семь RR instructions: ideal RAM **2 CPI**, legacy FRAM **107 CPI**,
sequential **40.0625 CPI**, prefetch **39.078125 CPI** (756,433 instr/s).
BR_self остаётся 107 CPI во всех FRAM modes. Формат RR loop: 63 operations
+ BR; 64 warmup, 4096 measured для RAM, 512 для FRAM.

Новые memory workloads: 31 operation + BR, 32 warmup и 256 measured
retirements. Initial registers/memory совпадают с CP7 ниже. Циклы включают
все operand/extension accesses. У BIT нет destination write; BIC/BIS/SUB
используют read + write. Полные JSON содержат также CS/SCK counts.

| Workload | Ideal RAM CPI | Legacy FRAM CPI | Sequential CPI | Prefetch CPI | Beats/instruction | Calculated instr/s @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| BIT_mem_reg | 14.59375 | 221.3125 | 221.3125 | 221.3125 | 1.96875 | 133,567 |
| BIT_reg_mem | 17.5 | 224.21875 | 224.21875 | 224.21875 | 1.96875 | 131,836 |
| BIT_mem_mem | 19.4375 | 327.875 | 327.875 | 327.875 | 2.9375 | 90,156 |
| BIC_mem_reg | 14.59375 | 221.3125 | 221.3125 | 221.3125 | 1.96875 | 133,567 |
| BIC_reg_mem | 19.4375 | 344.34375 | 344.34375 | 344.34375 | 2.9375 | 85,844 |
| BIC_mem_mem | 21.375 | 448 | 448 | 448 | 3.90625 | 65,982 |
| BIS_mem_reg | 14.59375 | 221.3125 | 221.3125 | 221.3125 | 1.96875 | 133,567 |
| BIS_reg_mem | 19.4375 | 344.34375 | 344.34375 | 344.34375 | 2.9375 | 85,844 |
| BIS_mem_mem | 21.375 | 448 | 448 | 448 | 3.90625 | 65,982 |
| SUB_mem_reg | 14.59375 | 221.3125 | 221.3125 | 221.3125 | 1.96875 | 133,567 |
| SUB_reg_mem | 20.40625 | 345.3125 | 345.3125 | 345.3125 | 2.9375 | 85,604 |
| SUB_mem_mem | 22.34375 | 448.96875 | 448.96875 | 448.96875 | 3.90625 | 65,840 |

**154 runs на каждую ROM-модель:** 27 RR RAM, 27 RR FRAM и 100 EA.
Portable/vendor counts совпадают. Все прежние 82 CP7 runs повторены без
изменения counts. [Raw results](benchmarks-cp8.json) и
[verification/source hashes](verification-cp8.json).

EA fixture totals (не stream CPI): 31671 завершённый case, 137006 bus beats,
948247 RAM clocks с 0..3 waits, 14739672 FRAM clocks. 627 abort/I/O candidates
исключены явно до Stage 2. Сравниваются registers, PSW и полные bus traces.

CP8 targets: `make benchmark benchmark-fram benchmark-ea`, соответствующие
`vendor-*` targets. `tools/record_cp8.py` сверяет результаты с portable copies
и прежним CP7 archive, сохраняет raw data, compressed fixtures/cycles и hashes.
Перед vendor runs сохранять JSON/CSV с суффиксом `-portable`, как описано ниже.

## CP7: все word addressing modes для MOV/ADD/CMP

13 loops, 31 одинаковая instruction + BR назад, 32 warmup retirements,
затем 256 measured instructions. Immediate/indexed/absolute words входят
в SPI/memory counts, но не считаются отдельными PDP-11 instructions.
Initial R1=2000 hex, R2=4000, SP=6000; data words 2000..2ffe равны 1,
остальная RAM нулевая. Extensions обычно 0; immediate=5a5a, absolute=2000/4000.
Это небольшие synthetic loops, не запуск ОС или реальной полной программы.

Final: CP7e core+probe 583/277/4, 40 MHz PASS; CP7d core+FRAM+probe
861/393/4, nominal 29.56 MHz PASS, Fmax 30.658 MHz. Внешняя FRAM работает
при CPU/2 = 14.78 MHz nominal. Последняя колонка — расчёт из CPI,
не измерение на физической плате.

| Workload | Ideal RAM CPI | Legacy FRAM CPI | Sequential-only CPI | Final prefetch CPI | Memory beats/instruction | Calculated instr/s @29.56 MHz |
|---|---:|---:|---:|---:|---:|---:|
| MOV_mem_reg | 14.59375 | 221.3125 | 221.3125 | 221.3125 | 1.96875 | 133,567 |
| MOV_reg_mem | 16.53125 | 239.71875 | 239.71875 | 239.71875 | 1.96875 | 123,311 |
| MOV_mem_mem | 18.46875 | 343.375 | 343.375 | 343.375 | 2.9375 | 86,087 |
| ADD_mem_reg | 14.59375 | 221.3125 | 221.3125 | 221.3125 | 1.96875 | 133,567 |
| ADD_reg_mem | 20.40625 | 345.3125 | 345.3125 | 345.3125 | 2.9375 | 85,604 |
| CMP_mem_mem | 19.4375 | 327.875 | 327.875 | 327.875 | 2.9375 | 90,156 |
| MOV_immediate_reg | 16.53125 | 223.25 | 91.5 | 85.6875 | 1.96875 | 344,974 |
| MOV_indexed_reg | 17.5 | 325.9375 | 260.0625 | 260.0625 | 2.9375 | 113,665 |
| MOV_mem_indexed | 20.40625 | 447.03125 | 447.03125 | 447.03125 | 3.90625 | 66,125 |
| MOV_indexed_indexed | 23.3125 | 551.65625 | 485.78125 | 485.78125 | 4.875 | 60,850 |
| MOV_autoinc_autoinc | 21.375 | 346.28125 | 346.28125 | 346.28125 | 2.9375 | 85,364 |
| MOV_stack_push | 17.5 | 240.6875 | 240.6875 | 240.6875 | 1.96875 | 122,815 |
| MOV_absolute_absolute | 25.25 | 553.59375 | 487.71875 | 487.71875 | 4.875 | 60,609 |

Непрерывно разрешённый prefetch (`cp7c`) проигрывал sequential-only:
MOV_mem_reg 255.21875 вместо 221.3125, MOV_mem_indexed 510.96875 вместо
447.03125, MOV_indexed_indexed 548.75 вместо 485.78125. Лишние speculative
слова заканчивались перед их отбрасыванием. Control `prefetch=0` устраняет
эти потери, сохраняя retained READ. Без data access immediate stream всё ещё
получает выигрыш: legacy 223.25, sequential 91.5, final prefetch 85.6875 CPI.
Предыдущая unrestricted policy здесь давала 77.9375; это явный tradeoff.

Прежние RR benchmarks (63 RR + BR) повторены: ideal RAM 2 CPI, legacy FRAM
107, sequential 40.0625, prefetch 39.078125; BR_self 107 во всех FRAM режимах.
ROM portable/vendor counts совпадают. Raw results, включая rejected policy,
сохраняются в [benchmarks-cp7.json](benchmarks-cp7.json); source/log hashes —
в [verification-cp7.json](verification-cp7.json).
Воспроизведение: `make benchmark-ea`, `make vendor-ea-benchmark`; параметр
`EA_MODES` выбирает -1 ideal, 0 legacy, 1 sequential, 2 prefetch.

Успешные EA fixtures: 13,587, точно 56,057 bus beats, все 3×8×8 mode pairs.
RAM с 0..3 waits: 395,656 CPU clocks; FRAM final prefetch: 6,021,885 clocks.
Unrestricted policy требовала 6,557,055. Эти суммы включают холодный fetch
каждого отдельного fixture и не являются stream benchmark CPI.
255 trapping/I-O candidates исключены; architectural trap behavior не заявлен.

`tools/record_ea.py` проверяет логи, одинаковые portable/vendor JSON и per-case
CSV, покрытие всех mode pairs, source archives и соответствие текущих RTL/ROM
CP7d/e. Затем сохраняет агрегаты, compressed fixtures/cycles и manifest hashes.
Перед vendor runs portable `build/benchmarks.json`, `fram-benchmarks-{0,1,2}.json`,
`ea-benchmarks-{-1,0,1,2}.json` и `ea-cycles-{-1,2}.csv` сохраняются рядом с
суффиксом `-portable` перед расширением: vendor targets используют те же имена
выходных файлов. Логи полного regression и benchmarks сохраняются как
`build/cp7-tests.log`, `cp7-vendor.log`, `cp7-benchmarks-portable.log` и
`cp7-benchmarks-vendor.log`. Исходный rejected-policy benchmark архивирован
отдельно и не считается повторно проверенным с final policy.

## Исторический CP6: память существующей платы

MR45V100A, CPU **29,56 MHz nominal**, `CLK_DIV=1`, SPI **14,78 MHz**.
Это расчёт throughput из RTL counts; FPGA не программировалась.
Все конфигурации исполняют один M0 microcode image (23 слова).

| Workload | Legacy CPI | Sequential READ CPI | Sequential + prefetch CPI | Memory beats/instruction | Prefetch instr/s at 29.56 MHz |
|---|---:|---:|---:|---:|---:|
| MOV_RR_loop | 107 | 40.0625 | 39.078125 | 1 | 756,433 |
| ADD_RR_loop | 107 | 40.0625 | 39.078125 | 1 | 756,433 |
| CMP_RR_loop | 107 | 40.0625 | 39.078125 | 1 | 756,433 |
| mixed_RR_loop | 107 | 40.0625 | 39.078125 | 1 | 756,433 |
| BR_self | 107 | 107 | 107 | 1 | 276,262 |

Каждый run: 64 warmup, затем 512 retirements. RR loops включают BR назад
после 63 RR words. Legacy: 54,784 CPU clocks, 512 CS transactions, 24,576
SCK rises. Sequential: 20,512 / 8 / 8,448. Prefetch: 20,008 / 8 / 8,448.
Cold fetch остаётся 107 clocks; следующий RR instruction без заголовка
стоит 39 clocks в sequential mode и 38 с prefetch. В BR_self заголовок
нужен каждый раз; спекуляция блокируется на фронте записи PC.

Основной выигрыш **2,738×** относительно прежнего контроллера даёт сохранение
SPI READ. Дополнительный prefetch ускоряет этот loop на **2,52%** относительно
sequential-only (737,847 → 756,433 instr/s). В финальном одинаковом RTL это
стоит **42 LUT4 + 2 FF**, без EBR. Буфер оставлен переключаемым параметром
`MEMORY_MODE` для следующих EA/EIS measurements, где есть больше ALU работы
между словами. Глубокий cache на этом этапе не добавлен.

Проверка stream microprogram: opcode + immediate/displacement/absolute test
words, один READ, 226 CPU clocks до STOP; 112 SPI clocks после drain включают
ещё одно отброшенное speculative слово. Это проверка transport semantics,
не реализация addressing-mode ISA. В unit test уже заполненный PF даёт ACK
на первом demand edge и не читает дальше, пока слово не потреблено.

Portable и vendor DP8KC ROM дали идентичные counts. Источник всех чисел:
[benchmarks-fram.json](benchmarks-fram.json), логи
[portable](../tb/reports/cp6-benchmarks-portable.log) и
[vendor](../tb/reports/cp6-benchmarks-vendor.log).
Воспроизведение: `make benchmark-fram`, `make vendor-fram-benchmark`.
Final synthesis scopes: sequential 801 LUT4/390 FF/4 EBR, Fmax 31.219 MHz;
prefetch 843/392/4, 30.292 MHz; оба проходят 29.56 MHz.
Полная периферия, boot и RK сюда не включены. Сопоставимого AM4 RR-on-FRAM
benchmark пока нет; ускорение относительно AM4 не заявляется.

## Исторический M0: идеальная RAM

Измеряется один и тот же final encoding v3 core: **582 LUT4 / 277 FF / 4 EBR**
с synthesis probe, **40 MHz constraint PASS**, TRACE Fmax **41.195 MHz**.
Источник counts — RTL functional simulation; portable synchronous ROM и
Diamond DP8KC дали идентичный JSON. Частота из TRACE не подменяет board test.

## Циклы и пропускная способность

| Workload | Zero-wait microclocks / instruction | Two-wait microclocks / instruction | Random 0..3 waits microclocks / instruction | Memory cycles / instruction | Zero-wait instr/s at 40 MHz |
|---|---:|---:|---:|---:|---:|
| MOV_RR_loop | 2.0000 | 4.0000 | 3.5237 | 1.0000 | 20,000,000 |
| ADD_RR_loop | 2.0000 | 4.0000 | 3.5029 | 1.0000 | 20,000,000 |
| CMP_RR_loop | 2.0000 | 4.0000 | 3.5283 | 1.0000 | 20,000,000 |
| mixed_RR_loop | 2.0000 | 4.0000 | 3.5083 | 1.0000 | 20,000,000 |
| BR_self | 2.0000 | 4.0000 | 3.5093 | 1.0000 | 20,000,000 |

20 млн instr/s — расчёт **40 MHz / measured CPI 2** при zero-wait memory,
а не измерение на плате. При двух wait clocks — 10 млн instr/s. Нет prefetch;
два такта получены FETCH+decode на одном фронте и одной ALU execution word.
При N wait clocks получается 2+N microclocks/instruction.

Каждая строка измеряет 4096 instructions после 64 warmup retirements.
MOV/ADD/CMP loops содержат 63 указанные RR instructions и BR назад —
64 слова на итерацию. Mixed loop повторяет MOV R1, R2; ADD R3, R2;
CMP R1, R2; ADD R4, R1, последняя позиция заменена BR. BR_self — BR на себя.
Начальные R0..R6=0..6, R7=0, PSW=0. Reset/initialization не входят в counts.
Это небольшие synthetic loops; реальную программу для полной ISA M0 ещё
не исполняет. Число register ALU cycles отдельно подтверждено ISA tests.

Zero-wait: **8192 clocks / 4096 instructions / 4096 bus transactions**.
Two-wait: **16384 / 4096 / 4096**. В random режиме LCG seed 0x00111801,
wait states выбираются из bits25:24 после retirement и сохраняются до ACK.
`wait_mode=0` в JSON означает 0 waits, `1` — 2 waits, `2` — random 0..3;
это номера сценариев, а не буквальное количество wait states.
Все 15 runs проверяют `clocks = 2*instructions + wait_clocks`, отсутствие
writes и ровно одну accepted transaction на instruction.

Raw counts: [benchmarks-m0.json](benchmarks-m0.json).
Полный лог: [m0-benchmarks.log](../tb/reports/m0-benchmarks.log).
Воспроизведение: `make benchmark`, затем `make vendor-benchmark` при
доступных vendor models. Результат — `build/benchmarks.json`.
Testbench использует условный период 10 ns для functional simulation;
physical timing определяется отдельным MAP/PAR/TRACE, SDF/board execution нет.

## Корректность до измерения CPI

Existing `../core/core.c` DCJ11 собран с ENABLE_MMU=0 и вызывается один раз
на fixture через core_step. 6272 fixtures сравнивают R0..R7, PSW, memory
side effects и отсутствие abort/trap для поддержанного subset. MOV сохраняет C;
CMP считает Rs−Rd с C=borrow; PC operands видят PC после FETCH increment.
Особые тесты проверяют odd word, error ACK, 16-bit wrap и unsupported encoding.
PSW/MMU mode architecture, byte ISA, addressing modes и trap frames пока
не входят в этот differential coverage. Exact sources/log hashes находятся
в [verification-m0.json](verification-m0.json).

## Сравнение с предыдущими экспериментами

| Design / scope | LUT4 | FF | EBR | TRACE Fmax MHz | Сопоставимый RR CPI |
|---|---:|---:|---:|---:|---|
| uJ11 M0 + serial probe, four ISA classes | 582 | 277 | 4 | 41.195 at 40 MHz constraint | 2 with zero-wait RAM |
| microcpu Stable, whole board | 1095 | 431 | 7 | 37.627 | нет; 6 clocks относятся к native microcpu instruction |
| microcpu horizontal 36-bit experiment | 1036 | 433 | 4 | 39.798 | сопоставимые counts не найдены |
| AM4 direct minimal board, гораздо более полная ISA | 902 | 284 | 7 | 31.206 | сопоставимые counts не найдены |

Источники исторических чисел: [previous_experiments.md](previous_experiments.md).
Разные scopes/ISA/память не позволяют объявить speedup относительно AM4
или fit полного будущего uJ11. FRAM/SD/RK clocks — board workloads, не RR CPI.

## Оставшиеся workloads

Conditional branch loop, JSR/RTS, полноценный stack workload, MUL/DIV/ASH/ASHC
и реальная PDP-11 программа ожидают соответствующих ISA stages. CP7 выше
добавляет memory directions и word MOV stack push; это не полная stack ISA.
CP6 выше отдельно измеряет opcode/extension stream, PC redirects и SPI FRAM.
