# Предыдущие эксперименты: исходные данные uJ11

> CP31 продолжает работу с FIS: FP11 удалён из активной сборки и сохранён
> в истории CP30. Full board после удаления — 1224 LUT / 326 FF / 6 EBR;
> после переноса RK CSR из FRAM в firmware EBR — 1252 / 326 / 6, 30.917 MHz.
> Первый изолированный MMU18 probe проверен и измерен; полный MMU fit
> ещё не доказан. [Отчёт CP31](mmu.md).


Аудит 2026-09-08 выполнен **до написания RTL uJ11**. Цель — первый
измеряемый integer execution engine с 16-битным физическим адресом.

## Источники и достоверность

* `microcpu`: `/Users/sash/Work/FPGA/microcpu`, HEAD
  `93985544221891e8e625850e74fe0426247f0849`.
* Текущий `k1801vm1`: HEAD `20c23ec4491b3a208e3bc9b9419248dbd130f4ca`.
  В рабочем дереве есть изменения пользователя, включая AM4; они не менялись
  для uJ11. HEAD не является идентификатором всех локальных отчётов.
* AM4: `../../lsi11-fpga/rtl/experimental/am4/`,
  `../../lsi11-fpga/ucode/experimental/am4/`.
  Исходная реализация — `1801BM1/cpu11`, revision
  `e0576637a35f0378f85673213a808098b9535526`, CC BY 3.0.
* C reference: `../../core/core.c`, `core.h`, `pdp11_fp.c`,
  `../../tests/core_tests.c`; DCJ11, сборка `ENABLE_MMU=0`.
* `microasm` — ассемблер native ISA microcpu в `microcpu/asm/microasm.c`.
  `../../microasm11/microasm11.c` — **другой ассемблер**, для PDP-11 ISA.
  Ни один из них не является готовым ассемблером 36-битных слов AM2901.

Ссылки ниже на `microcpu` абсолютные: это соседний, не вложенный репозиторий.
Все старые числа — исторические измерения, не повторный синтез uJ11.
Числа LUT4 из MAP включают distributed RAM и ripple logic; число
`ORCALUT4` из Synplify hierarchy с ними напрямую не сравнивается.
Почти все прошлые проекты — **CPU вместе с платной обвязкой**, не bare core.
Fmax TRACE — расчёт по размещённой схеме, не измеренная частота платы.
«—» означает отсутствие проверенного числа, а не ноль.

## microcpu: что действительно синтезировалось

| Вариант / checkpoint | LUT4 | FF / registers | EBR | TRACE Fmax, MHz | Источник |
|---|---:|---:|---:|---:|---|
| Preserved J11, `0522437` | 1279 | 462 | — | 30.443 | [fpga-j11.md](/Users/sash/Work/FPGA/microcpu/docs/fpga-j11.md) |
| Preserved J11 после sharing adders | 1138 | 377 | 7 | 28.103 | тот же, Historical HC1200 checkpoint |
| Preserved J11, RS/HALT, LUT context 64 words | 1213 | 378 | 7 | 27.715 | тот же, 2026-08-28 register-set checkpoint |
| Preserved J11, context в хвосте EBR | 1085 | 378 | 7 | 34.204 | тот же, Shared code/context RAM |
| New `ucode` stage 1, short constants/context | 964 | 379 | 7 | 41.432 | [cpu-profiles.md](/Users/sash/Work/FPGA/microcpu/docs/cpu-profiles.md), raw `impl1-ucode` |
| `ucode` stage 2, compact CALL/JMP | 1013 | 385 | 7 | 48.195 | тот же, raw `stage2` |
| `ucode` stage 3/4, word PC | 959 | 381 | 7 | 45.708 | тот же, raw `stage3-wordpc`, `stage4` |
| `ucode` word PC + ADC/SBC probe | 1016 | 382 | 7 | 42.385 | тот же, raw `stage3-carry`, rejected |
| `ucode` SD without FIS | 1041 | 423 | 7 | 41.943 | raw `impl1-sd-nofis`, [previous_reports.json](previous_reports.json) |
| `ucode` density follow-up, no disk | 1008 | 382 | 7 | 43.083 | cpu-profiles.md, historical report |
| `ucode` density follow-up, SD+FIS | 1099 | 429 | 7 | 39.987 | тот же, предшествует Stable |
| Stable `ucode`, SD+FIS+RT-11 | 1095 | 431 | 7 | 37.627 | [ucode-cpu.md](/Users/sash/Work/FPGA/microcpu/docs/ucode-cpu.md) |
| Compound issue / VLIW 18-bit | 1256 | 438 | 7 | 36.670 | [VLIW-EXPERIMENT.md](/Users/sash/Work/FPGA/microcpu/docs/VLIW-EXPERIMENT.md) |

Для legacy `rtl/cpu.v` найден исходник и тесты, но отдельные верифицированные
цифры bare-core HC1200 в изученных отчётах не установлены. Нельзя приписывать
ему показатели `j11_microengine.v` или `ucode_cpu.v`.

Legacy использует native ISA, восемь рабочих регистров, один RF read port и
многофазное чтение операндов. Preserved J11 и Stable `ucode` исполняют
J-11 **программой на native ISA**, а не прямыми микрокомандами ALU.
Обычная native инструкция занимает шесть FPGA clocks. Guest instruction
может требовать много таких инструкций. Шесть clocks — не CPI PDP-11.

Улучшение 1279 → 1138 LUT получено sharing ALU/memory adders, одним PC adder,
убиранием дублирующих memory/operand registers и узким SPI divider.
Цена: Fmax 30.443 → 28.103 MHz. Перенос context из LUT RAM в свободный хвост
семи EBR дал 1213 → 1085 LUT и поднял Fmax до 34.204 MHz без изменения
шеститактной native последовательности. Distributed RAM: 120 → 24 LUT.

Stable: физически 3584×18 в семи EBR, логически 16-bit code, хвост 64 слова —
context, code limit 3520. Исторический образ `a985039`: 3501 code words;
VLIW study использует другой образ, 3504 words. Эти размеры не смешиваются.
Context содержит guest registers, alternate R0–R5, K/S/U SP, PSW и I/O state.
ISA, EA, banking, traps, EIS/FIS реализованы ассемблером. Есть физический
RT-11 boot и differential suite: 209/209 snapshots, 29 EIS cases;
отдельно 4040/4040 FIS reference cases в preserved experiment.

Подробности microasm/ucode stages: LDI8 и direct context сократили 3463→3317
code words, compact CALL/JMP — 3317→2968 за +49 LUT. Word PC убрал 54 LUT
без изменения code size. ADC/SBC отдельно стоили +57 LUT ради 14 слов и были
отвергнуты; позже их приняли вместе с CBZ/CBNZ и FIS shifts, когда общий
выигрыш 144 words позволил вместить SD+FIS. Это пример того, почему стоимость
opcode следует оценивать вместе с реальным microcode, а не изолированно.
Fast-bus benchmark с reset/checks: preserved 903548 → stage4 814640
(−9.84%) → density 783572 (ещё −3.81%). Ассемблерные тесты покрывали
profile selection, range/alignment, relocation и object versions; native
проверки включали 115255 ALU/address и 163840 PC/target cases.

### Compound issue: выигрыш и цена

В 3504-словном образе найдены 444 пары: 317 compare+branch и 127 result+GSET.
Пары используют два свободных физических бита EBR. Один шеститактный проход
исполняет пару. Развёртывание существующего native stream в 36-bit bundles
дало бы 3063 bundles и не помещалось в доступную microstore.

| Нагрузка | Scalar clocks | Pair clocks | Выигрыш |
|---|---:|---:|---:|
| Guest benchmark | 830856 | 754464 | 9.19% |
| RK611 scenario 0 | 916723 | 871823 | 4.90% |
| FIS main | 840746 | 766670 | 8.81% |
| Full scripted RT-11 | 1614712535 | 1483644906 | 8.12% |
| Two-sector bootstrap | 1688275 | 1688275 | 0% |

Цена +161 LUT, +7 FF, −0.957 MHz; осталось 24 LUT и 10 slices. Это не
приемлемый запас для uJ11. Нельзя переносить эти относительные ускорения на
новую машину с другой памятью и другой длительностью микрокоманды.

## Native PDP-11 experiments

Источник всех строк этой таблицы:
[J11-NATIVE-EXPERIMENT.md](/Users/sash/Work/FPGA/microcpu/docs/J11-NATIVE-EXPERIMENT.md).
Проекты содержат FRAM/SD/UART/timer и intentionally nonbootable CPU subset.

| Вариант | LUT4 | FF | EBR | Fmax, MHz | Итог |
|---|---:|---:|---:|---:|---|
| Broad behavioral, pipelining | 6069 | 1465 | 1 | — | MAP overflow |
| Broad behavioral, area | 5841 | 797 | 1 | — | MAP overflow, нет PAR/Fmax |
| Compact initial | 751 | 410 | 2 | 43.917 | узкий subset |
| Compact private read | 828 | 432 | 2 | 41.264 | +77 LUT |
| Compact BIT/branches | 876 | 435 | 2 | 47.524 | +48 LUT |
| Compact word ALU | 1017 | 437 | 2 | 48.132 | stop growth |
| Explicit 21-bit onehot | 998 | 453 | 2 | 38.052 | −19 LUT, timing хуже |
| Split state / automatic encoding | 1044 | 453 | 2 | 50.020 | площадь хуже |
| Split state / sequential | 1073 | 437 | 2 | 43.970 | оба хуже |
| 18-bit vertical control | 1007 | 433 | 3 | 43.858 | −10 LUT за 1 EBR |
| 36-bit horizontal control | 1036 | 433 | 4 | 39.798 | хуже обоих |
| Active PC/PSW statecache | 995 | 417 | 2 | 43.892 | экономия лишь 10 slices |
| 8-bit ALU, initial | 961 | 422 | 2 | 47.842 | два ALU phases + writeback |
| 8-bit ALU, linear context / branch factoring | 938 | 422 | 2 | 48.473 | 469 slices, accepted gate |
| Source mode 1 | 973 | 422 | 2 | 42.622 | уже выше gate 470 slices |
| Full mode 1 | 1058 | — | — | — | 530 slices |
| Firmware dynamic guest bus | 1009 | 422 | 2 | 45.788 | 505 slices |

Аннотация `syn_encoding="onehot"` на исходном mixed process ничего не
изменила: 1017 LUT / 48.132 MHz. Вариант early NZVC commit убрал четыре
staging FF, но вырос до 972 LUT / 486 slices, Fmax 44.691 MHz; его отменили.
Специальный mailbox для firmware bus потребовал 537 slices, после reuse —
523; обычный общий bus path оказался дешевле (505).

Причины провала broad prototype: два больших RF, mux guest/firmware,
дублирование EA/opcode/flags/writeback. Даже 5841 LUT — 456% HC1200.
Compact убрал это через synchronous context EBR, но каждая новая функция
добавляла hardwired decisions. CMP/BIC/BIS/CLR стоили 141 LUT.

18-bit control содержал имя шага, next, RAM controls и firmware-fetch.
Сохранился step decoder с fanout до 90. 36-bit вариант заменил его 19
onehot actions, fanout снизился до 41, но conditional datapath mux остались.
CPU hierarchy: 591 ORCALUT4 hardwired → 580 vertical → 605 horizontal.
Critical path vertical — control EBR → next mux → EBR address.
**Ширина 36 сама по себе не устраняет дорогой datapath/control.**

Directed SXT service: compact 267 clocks / 46 firmware fetches;
vertical и horizontal 268 / 46, дополнительный clock — reset ROM priming.
Statecache 279 / 46. Firmware-only emulation трёх `MOV R1,(R2)` случаев:
636 clocks / 83 firmware fetches, при неизменном RTL.
RT-11 opcode trace: 766682 из 767129 (99.94%) распознаны broad prototype,
но это не доказательство traps/IRQ/modes или загрузки ОС.

## AM4: формат, datapath и sequencing

Изучены `am4_direct.v`, `am4_alu.v`, `am4_seq.v`, `am4_plm.v`,
`mc.asm`, `tools/am29_m4.def`, assembler и EBR generator, а также bus,
MOVB, interrupt/RTI, EIS/FIS и vendor-ROM testbenches.

AM4 — 1024×56 synchronous MicROM, RF 16×16, два asynchronous read ports,
запись всегда по B, Q16. Три поля Am2901: source[2:0], function[5:3],
destination[8:6], отдельный carry-in[9]. A/B имеют четыре literal bits и
по одному dynamic-select bit. Есть combined RAM/Q shift, без MUL/DIV unit.
PSW и shift carry раздельны, byte flags используют границу bit7/8.

Поля MicROM: ALU[8:0], carry[9], dynamic A/B[11:10], A[15:12], B[19:16],
sequencer[23:20], OR/condition[28:24], PSW/IR/control[32:29], D mux[34:33],
bus[43:35], next[53:44]. Верхние поля перекрываются с D immediate и shift
pattern. `am29_m4.def` и `meta29.py` собирают независимые фрагменты одного
слова, проверяя конфликт заданных bits; это полезнее для uJ11, чем перенос
native microcpu instruction decoder.

| OR modifier | Фактическая RTL функция | Польза |
|---|---|---|
| `OR_MS` | `IR[11:9]` | source EA, таблица 8 входов |
| `OR_MD` | `IR[5:3]` | destination EA |
| `OR_RR` | `{0, dst_mode==0, src_mode==0}` | обход memory paths |
| `OR_BT` | `{0, address[0], decoded_byte}` | byte lanes и word/byte |
| `OR_R67` | `{0,0,~(alu_a[2]&alu_a[1])}` | шаг byte для R0–R5 / SP, PC |

`OR_RR` predicates в RTL равны **mode==0**: соседние комментарии с `not`
не являются спецификацией. `OR_R67` смотрит **выбранный A selector**,
не безусловно IR source. У temp-регистров тоже есть такие low bits;
микрокод обязан вызывать этот dispatch в правильном контексте.
В HC1200 бывший `OR_LD` стал decoded-byte `OR_BF`, а `OR_AP` использует
специальную сигнатуру исправления MOVB. Эти исторические encoding tricks
не следует буквально переносить в uJ11.

Dynamic A: IR source либо destination выбирается control bits; младший
бит можно OR-нуть единицей для нечётного регистра пары EIS. B делает
аналогичный выбор с другой полярностью. Это маленькие selector mux, а не
копирование операндов в дополнительные registers перед каждой инструкцией.

Sequencer Am2909/29811: 10-bit PC, AR, четыре return slots, условный
выбор PC/AR/stack/direct, OR младших bits, push/pop и 8-bit loop counter.
Opcode PLM выдаёт 7-bit entry и decoded-byte; entry XOR 0x11 сдвигается
на три бита, младшие bits берутся из MicROM. ROM получает **следующий**
адрес перед фронтом, поэтому отдельной fetch-фазы микрокоманды нет.
PC sequencer содержит `y+1`, то есть это не обычная пара uPC→ROM→uIR
с дополнительным bubble.

EA: L2E8…L2EF — source dispatch; L2F0…L2F7 — destination/read dispatch.
Mode 0 читает RF; mode 1 — RF address; 2/4 меняют base register на 1/2;
3/5 читают pointer и всегда меняют base на 2; 6/7 читают extension по PC,
складывают с base и при необходимости читают pointer. PC relative считает
base после чтения extension. Store-only MOVB regression особенно важен:
лишнее чтение destination у memory-mapped I/O имеет наблюдаемые эффекты.

EIS: entry ASH 0x298, ASHC 0x2A0, MUL 0x2B0, DIV 0x2C8. MUL использует
Q, shared add/sub, combined right shift и `OR_TC` (carry и counter bit5);
DIV — serial shift/subtract и проверки знака/overflow, счётчик загружается
через ALU. ASH/ASHC маскируют count шестью bits; направление и выходящий
бит управляют single-bit shifts/flags. Это reference алгоритмов, не
обоснование переносить весь AM4 shift-pattern decoder.

Interrupt logic защёлкивает requests и выдаёт small priority index.
Microcode сохраняет PSW/PC в stack, читает vector, выполняет RTI/RTT.
Bus adapter держит registered request/address/data до ready, отдельно
хранит completion, чтобы stall не повторял transfer. Timeout original
microcode получает как condition. AM4 PSW8 и IRQ masking не равны полной
J-11 priority/mode architecture; это нельзя объявлять J-11 совместимостью.

## AM4 / LSI-11 synthesis

| Конфигурация | LUT4 | FF | EBR | Fmax, MHz | Источник |
|---|---:|---:|---:|---:|---|
| MCP-1600 LSI11 board | 1273 | 274 | 6 | — | [LSI11-EXPERIMENT.md](/Users/sash/Work/FPGA/microcpu/docs/LSI11-EXPERIMENT.md), 640/640 slices |
| AM4 original adapter, minimal board | 903 | 285 | 7 | 29.574 | raw `original-am4-db546ca5`, [previous_reports.json](previous_reports.json) |
| AM4 direct adapter, minimal board | 902 | 284 | 7 | 31.206 | raw `direct-am4-11c3e176`, 456 slices |
| AM4 FRAM board | 1058 | 393 | 7 | 30.469 | raw `fram-am4-331d6776` |
| AM4 firmware SD bootstrap | 1186 | 475 | 7 | 30.425 | [diamond.md](/Users/sash/Work/FPGA/microcpu/docs/diamond.md) и raw `sdboot-am4-93ce2d5b` |
| AM4 hardware SD loader | 1452 | — | — | — | [AM4-EXPERIMENT.md](/Users/sash/Work/FPGA/microcpu/docs/AM4-EXPERIMENT.md), 728/640 slices |
| AM4 firmware RK READ | 1256 | 483 | 7 | 30.854 | тот же, 633/640 slices |
| Standalone historical physical 26.60 MHz | 1263 | — | 7 | — | [PORTING-NOTES.md](../../lsi11-fpga/docs/PORTING-NOTES.md), 634 slices |
| Standalone documented 29.56 MHz clean gate | 1271 | — | 7 | — | [BUILD-DEBUG-PROGRAM.md](../../lsi11-fpga/docs/BUILD-DEBUG-PROGRAM.md), 639 slices |
| Standalone локальные MAP/TRACE | 1266 | 470 | 7 | 32.438 | `../../lsi11-fpga/boards/hc1200-microcomp/impl1-am4/microcomp-am4_impl1.{mrp, twr}`, 635 slices |

Последняя строка отличается от documented clean gate. Без привязки source
archive к отчёту нельзя объявлять её текущим или последним physical image.
`tools/audit_previous.py` сохраняет paths и hashes найденных raw reports.

Широкая inferred ROM AM4 ушла в LUT. Явные семь 1024×9 DP8KC исправили
mapping. Свободные 7 physical bits использованы для bootstrap/RK firmware.
Это подтверждает смысл **четырёх 1024×9 EBR для uJ11**, а не уменьшения
36→32 при той же физической глубине. Последовательные FRAM transactions
требовали 674 slices, read-only версия 672; rejected. Layout одного
firmware literal менял fit 640→643 slices — предупреждение о routing margin.
AM4 FRAM directed execution: 5960 clocks / 32 SPI transactions;
SD success: 1544300 clocks; RK READ: 1478600 clocks. Это board workloads,
не register-register CPI AM4. Сопоставимого измеренного RR CPI не найдено.

## Решения для первого uJ11

1. Прямое управление общим datapath из ROM. Не хранить в ROM только имя
   hardwired state и не исполнять guest ISA через шеститактную native ISA.
2. RF16×16, два чтения, одна запись; сначала измерить цену distributed RAM.
   Не добавлять guest/firmware RF или большие EA/writeback mux.
3. NEXT-address feedback synchronous ROM; отдельно измерить critical path.
4. Control format 36 bits — гипотеза для проверки, не доказанная оптимальность.
5. Первый ISA subset только FETCH, word MOV/ADD/CMP mode0 и BR. CMP —
   **source минус destination**, C=borrow; MOV сохраняет C.
6. Полная J-11 banking architecture нужна позже для совместимости, но не
   первому RR тесту. Активный RF остаётся один; редкий обмен bank по microcode.
7. Первый synthesis checkpoint до расширения datapath/ISA. Внешняя обвязка,
   debug probes и конфигурация должны быть явно указаны в новых измерениях.
8. В uJ11 v1 нет MMU, translation, mode memory spaces, separate I/D,
   18/22-bit addresses, tables или зарезервированной под них памяти.

## Сопоставление с полученным uJ11 M0 (после аудита)

2026-09-08 после восстановления доступа к Diamond-хосту выполнены 12 новых
MAP/PAR/TRACE runs с отдельными source archives, включая неудачные варианты.
Итог M0: **582 LUT4, 277 FF, 4 EBR, 40 MHz constraint PASS, TRACE Fmax
41.195 MHz**, 23 microcode words, 2 clocks/PDP-11 instruction при zero-wait RAM.
В FF входят 189 bits input/observation harness; 88 state bits — engine.
Данные и hashes: [synthesis.md](synthesis.md), [benchmarks.md](benchmarks.md).

Это меньше ресурсов, чем historical AM4 direct minimal board
(902/284/7, 31.206 MHz) и native microcpu Stable whole board
(1095/431/7, 37.627 MHz), но scopes и полнота ISA различны. M0 исполняет
только MOV/ADD/CMP R, R и BR; AM4 обслуживает существенно больше ISA и board
функций. Утверждение о превосходстве полного uJ11 по скорости/ресурсам ещё
невозможно. Общего RR benchmark AM4 не найдено, ratio throughput не вычисляется.

Исторический horizontal microcpu experiment стоил 1036 LUT4/433 FF/4 EBR,
39.798 MHz и почти не устранял datapath mux. В uJ11 изменения именно mux
дали измеримый эффект: engine CP3 661→610 LUT4 после parallel buses/aligned
selectors, затем полный core 639→582 LUT4 после encoding FETCH как обычного
ALU ADD. Это подтверждает необходимость измерять wiring/decoding, а не
считать саму ширину microinstruction доказательством эффективности.

## Дополнение CP6: FRAM и периферия существующей платы

Подробно проверены актуальные `lsi11-fpga` FRAM transport, AM4 board bus,
KL11, KW11-L, panel GPIO, SD byte service, boot overlay и RK service mapping.
Их адреса, побочные эффекты и hashes: [fram-peripherals.md](fram-peripherals.md)
и [fram-source-audit.json](fram-source-audit.json). Исторические не вместившиеся
sequential-access варианты AM4 занимали 674/640 slices и 672/640 read-only
(источник — `lsi11-fpga/docs/PORTING-NOTES.md`). Поэтому новый FRAM тракт
проверялся отдельными gates, а не добавлялся к готовой полной ISA.

С тем же M0 CPU неизменённый legacy FRAM controller дал 107 CPI и
739 LUT4/372 FF/4 EBR. Финальный uJ11 sequential+prefetch: 843/392/4,
30.292 MHz TRACE, nominal 29.56 MHz PASS, 39.078125 CPI в RR loop.
Это сопоставимые uJ11 memory experiments, а не измеренный speedup над AM4.
Полные raw reports, неудачный timing вариант и метод counts — в
[synthesis.md](synthesis.md) и [benchmarks.md](benchmarks.md).

## Дополнение CP7: микрокодные addressing modes

Все word source/destination modes для MOV/CMP/ADD добавлены без расширения
RF, ALU, engine или microsequencer: 23→143 microcode words, core + probe
582→583 LUT4, прежние 277 FF и 4 EBR, 40 MHz PASS (TRACE 41.810 MHz).
Это прямое измерение стоимости микрокодных EA в текущем datapath.

С FRAM финальный scope стоит 861 LUT4/393 FF/4 EBR, 29.56 MHz PASS,
TRACE 30.658 MHz. Относительно CP6 — +18 LUT4 и +1 FF. Первый CP7 FRAM
вариант занимал 825 LUT4, но prefetch перед operand accesses ухудшал CPI;
control policy стоила дополнительных 36 LUT4 и 1 FF. MOV memory→register
исправлен с 255.21875 до 221.3125 CPI, RR остался 39.078125 CPI.

13,587 успешных EA cases сверены с существующим DCJ11 по registers, PSW
и 56,057 bus beats на RAM/FRAM с обеими ROM models. 255 trap/I/O candidates
исключены до Stage 2. Это по-прежнему часть ISA, без полного board top;
сравнение с более полными AM4/microcpu не доказывает преимущество всей системы.

## Дополнение CP8: word BIT/BIC/BIS/SUB

Pair BA заменяет DZ в v4 без расширения microinstruction и ALU. Первая
проверка до ISA: core 600 LUT4/277 FF/4 EBR, 40 MHz PASS; FRAM 850/393/4,
29.56 MHz PASS. После семи word classes microcode занимает 214 words,
core 600/277/4, FRAM **878/393/4**, 29.56 MHz PASS, Fmax **31.540 MHz**.
Относительно CP7 оба scopes выросли на 17 LUT, без новых FF/EBR.

Все RR instructions сохраняют 2 CPI ideal RAM и 39.078125 CPI FRAM loop.
Прежние CP7 memory benchmarks не замедлились. Проверены 31671 EA и 12928
RR/BR cases против DCJ11. Отдельный core при 40 MHz не проходит на 0.028 ns;
сохранённый placement прошёл TRACE при 39 MHz. Неудачный новый fit при
39 MHz также сохранён. Полные цифры и метод — в synthesis/benchmarks docs.

## Дополнение CP9: unary/branches и цена FRAM speculation

12 unary word classes и 15 branch classes добавили 128 words (214→342)
без изменения datapath/RF/Q/PSW/sequencer RTL. Unary промежуточный gate:
core 615/277/4, 40 MHz FAIL, Fmax 38.918; FRAM 876/393/4,
29.56 MHz PASS, Fmax 30.832. После branches: core 615/277/4,
40 MHz PASS, 40.925; FRAM 910/393/4, 29.56 MHz PASS, 30.876.
Относительно CP8 — +15 LUT core и +32 LUT FRAM, прежние FF/EBR.

Timing PASS не означал хорошую FRAM performance: unrestricted speculation
на BNE self-loop давала 144 CPI вместо 108 sequential-only. Микрокодная
пауза на CJUMP исправила это до 108 CPI; countdown DEC/BNE ускорился
с 88.176471 до 72.352941 CPI, ценой 1.9375 CPI на not-taken BNE loop.
Повторный MAP/PAR/TRACE CP9e/f подтвердил неизменные resources/timing.

Все прежние CP8 workloads сохраняют counts. Сравнение с историческим
AM4/microcpu остаётся разными scopes: полная board периферия, полная ISA
и physical-board benchmarks ещё не входят в CP9. Прямой общий speedup над
AM4 из этих цифр не выводится. [CP9 details](single-operand-branches.md).


## Дополнение CP10: byte ISA и реальная цена redirect

Byte primitives проверены отдельным CP10a: 434 LUT/138 FF/0 EBR,
50 MHz PASS/52.348 MHz (scope включает probe; прежний CP2r1 предшествует BA).
MOVB core 675/277/4 не прошёл 40 MHz, FRAM 928/393/4 прошёл 29.56 MHz.
Parallel D mux дал FRAM 961/393/4 и худший Fmax, поэтому отвергнут.
Полная byte ISA использует те же 342 microinstructions; её первый FRAM
fit 934/393/4 не прошёл 29.56 MHz (29.402). Grouped compare также FAIL.

Регистрация prefetch PC mismatch: **905 LUT/394 FF/4 EBR, 29.56 MHz PASS,
TRACE 32.047 MHz**. Core: **659/277/4, 35 MHz PASS/36.496 MHz**.
Один дополнительный FF разрывает позднюю цепочку управления valid-флагами.
Все 430 CP9 benchmark runs сохраняют counts; 176 byte runs добавлены.
Oracle: 90177 завершённых cases на каждую RAM/FRAM × portable/vendor ROM.

Все 46 source/report archives, включая failed experiments, сохранены.
Сравнение с AM4/старым microcpu по-прежнему требует учёта разных scopes;
byte support не означает реализацию trap/privilege architecture или MMU.


## Дополнение CP11: JMP/JSR/RTS/SOB и цена быстрого SOB

Микрокодный baseline 361 words: core 673/277/4, 35 MHz PASS/36.817;
FRAM 933/394/4, 29.56 MHz PASS/31.257. SOB с PSW save/restore — 8/9 CPI
при ideal RAM. Прямая проверка ALU Z сократила это до 2/3 CPI, но core
674/277/4 не прошёл 35 MHz (33.929); FRAM 971/394/4 прошёл 29.56 MHz.

Ранняя проверка RF[A]==1, encoding v7: core 694/277/4, 35 MHz PASS/36.358;
FRAM 970/394/4, 29.56 MHz PASS/31.672, 355 words. Это +35/+65 LUT
относительно CP10, без новых FF/EBR; прежние 606 benchmark runs не изменены.
104991 completed oracle cases на каждую RAM/FRAM × portable/vendor ROM.
Полный board scope AM4/microcpu пока несопоставим с uJ11 probe; общего
speedup над AM4 эти результаты не доказывают. [Control flow](control-flow.md).


## Дополнение CP12: SWAB/SXT/MARK и parallel decoder

Добавлены только 38 microinstructions и opcode dispatch; остальной RTL
сохранён. Priority variant: core 708/277/4, 35 MHz PASS/37.012;
FRAM 1081/394/4, 29.56 MHz PASS/30.650. FRAM рост +111 LUT против CP11
остановил расширение до исследования. Уже Synplify ORCALUT4 вырос 853→964.

Parallel class masks сохранили все decoder outputs и дали core 708/277/4,
35 MHz PASS/35.723; FRAM 984/394/4, 29.56 MHz PASS/31.287.
Это −97 LUT против priority FRAM и +14 LUT против CP11. 393 words, v7.
Все 638 CP11 benchmark counts и 355 прежних microinstructions сохранены;
109515 completed oracle cases на каждую RAM/FRAM × portable/vendor ROM.
SWAB register loop с FRAM: 40.15625 CPI, восемь microcoded RF/Q shifts,
без hardware byteswap. [CP12 details](extra-instructions.md).


## CP13: software traps и RTI

29 новых микрокоманд, прежний datapath/sequencer, 422 words v7.
Core+probe 735 LUT4 / 277 FF / 4 EBR: 35 MHz PASS, TRACE 36.340 MHz.
FRAM+prefetch+probe 999 / 394 / 4: 29.56 MHz PASS, TRACE 31.245 MHz.
Относительно CP12 +27/+15 LUT, FF и EBR сохранены.

114155 completed DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM;
690 benchmark runs на ROM-модель, все 666 прежних counts неизменны.
Ideal RAM trap/RTI: 17/8 microclocks. Проверены 112 directed fault cases
и nested BPT/IOT с двумя RTI. Профиль kernel bank0, T=0; IRQ и vector004
ещё не реализованы. [CP13 details](software-traps.md).


## CP14: IRQ/WAIT/SPL и KW11/KL11 resolver

Core 764 LUT4 / 293 FF / 4 EBR: 35 MHz PASS, TRACE 36.720 MHz.
Generic IRQ FRAM scope 1038/410/4: 29.56 PASS, TRACE 30.992 MHz.
FRAM + resolver для BR4/BR6 1016/410/4: 29.56 PASS, TRACE 31.221 MHz.
Разные разрешённые priority values и probe ports объясняют возможность
меньшего fit с resolver; это не отдельная «отрицательная стоимость» адаптера.
Полная board peripheral logic не входит в эти scopes.

446 words v8, +24 к CP13. Реальный state core +2 FF, adapter +1 FF.
120809 completed DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM;
714 benchmark runs на ROM-модель, все 690 прежних counts сохранены.
WAIT/RTI/BR с IRQ: 9.333333 CPI ideal RAM, 335.666667 с prefetch FRAM.
Nested IRQ/IRQ/RTI/RTI/BR loop включает ADD и даёт 13 CPI ideal RAM.
[CP14](interrupts.md). MMU, HALT/trace/RTT и EIS отсутствуют.


## CP15: reserved traps, entry placement и shared predicate

Добавлены четыре microinstructions, state/FF/EBR сохранены. Базовый decoder:
core 772/293/4, 35 PASS/36.476; FRAM 1049/410/4, 29.56 PASS/31.944.
High entries3fa/3fc: core 779/35.828, FRAM 1043/30.929, прежние FF/EBR;
вариант отвергнут по совокупности area и timing margin.

Shared JMP predicate и entries040/042: core 778/293/4, 35 PASS/36.236;
FRAM 1036/410/4, 29.56 PASS/31.697. Выбран этот вариант: −13 LUT FRAM
против baseline, при +6 LUT core; все 65536 decoder outputs эквивалентны.
450 words v8, +4 к CP14. 124969 completed DCJ11 cases; 726 benchmark runs
на ROM-модель, все 714 прежних counts сохранены. [Подробности](reserved-traps.md).

## CP16: memory-fault redirect и autoincrement на ошибке

CP16a core 791/294/4, 35 PASS/36.831; CP16b FRAM/resolver 1062/413/4,
29.56 PASS/31.083. Timing успешен, но новый oracle отверг состояние
mode2/3 при неуспешном READ. Архивный вариант воспроизводимо теряет +2 к R0.

CP16c/d добавляют READ fault_inc и один repair FF, сохраняя обычный prefetch:
core 792/295/4, 35 PASS/36.988; FRAM 1030/412/4, 29.56 PASS/31.079.
Всего два новых core state FF и два microcode words к CP15; 452 words v9.
Это measured whole-design fit, без вывода об отрицательной LUT цене repair.

Все 124969 прежних completed cases и 726 benchmark runs сохранены.
32780 новых fault-frame comparisons, 3536 repair cases, 8 I/O exclusions.
Состояние сверяется с read-only hook в конце core_take_vector; 296 последующих
изменений C emulator после abort и 4 дополнительных bus reads сохранены отдельно.
Первые 16 FRAM-system tests проверяли actual I/O error, handler EA CALL, RTI и masked IRQ;
96 directed cases — defined terminal double-fault policy. MMU не добавлялась.
[Отчёты и ограничения](memory-faults.md).

Unmasked fault-vector IPL0 выявил ранний IRQ в CP16c/d. Дополнительный bit
различает IRQ и memory-fault frames. CP16e core 809/296/4, 35 PASS/36.552;
CP16f FRAM 1042/413/4, 29.56 PASS/31.117. Это принятый вариант CP16:
+3 core FF к CP15, те же 452 words. 8 primary DCJ11 tests, 32 FRAM-system
cases, прежние 726 benchmarks; оба ошибочных variants сохраняются как
reproducible negative controls. Полный fixture 32780 проверен заново на final RTL.

## CP17: trace и RTT за один FF

CP17a core 826/297/4, 35 MHz PASS/36.302; CP17b FRAM/resolver 1046/414/4,
29.56 MHz PASS/30.194. Всего +17 LUT core/+4 LUT FRAM и один FF к CP16.
Существующее irq_active=3 обозначает trace frame. Microstore 452 words v10:
только bit0 word037 изменён, все label addresses сохранены.

11196 новых trace/RTT cases, 276 abort/I/O exclusions с явными reason masks;
124969 прежних cases и 32780 fault frames неизменны. 746 benchmarks/ROM,
все прежние 726 сохранены. 24 FRAM-system и 140 trace/fault checks.
Критический FRAM slack уменьшился с 1.692 до 0.710 ns при 29.56 MHz.
Обычная ISA сохранила CPI; trace adds 16 ideal-RAM clocks frame, RTT — 8 CPI.
[Отчёт с ограничениями oracle и measured scope](trace-rtt.md).

## CP18: CC/NOP/MFPT, три microcode layouts

Финальные CP18e/f: core843/297/4,35 MHz PASS/35.674; FRAM1062/414/4,
29.56 MHz PASS/30.409. Новых RTL state registers нет,493 words v10.
Все прежние452 words/labels сохранены. CP18a/b:842/1083 LUT; перенос
в первую страницу CP18c/d:820/1134 LUT, после synthesis у FRAM416 FF.
Поэтому только core fit оказался недостаточным критерием; stride8 layout
CP18e/f выбран по итоговой FRAM area. Все шесть raw fits сохранены.

21120 новых DCJ11 cases без exclusions,157285 completed всего и отдельные
32780 fault frames на memory/ROM pair.766 benchmarks/ROM,746 прежних
counts неизменны. CC/NOP4 CPI ideal RAM, MFPT2. [Подробности](system-flags.md).

## CP22: исправление эталона и ASHC

CP22a/b получили856/299/4 и1098/416/4, TRACE37.258/30.457 MHz,
но были отклонены по семантике. Сверка J-11 instruction definition и SIMH
выявила ранний snapshot ASH/ASHC и ASHC N/Z после aliased stores в project C.
CP21 ASH тоже требовал исправления. Эти прежние differential PASS результаты
не доказывали верный alias ordering.

CP22c/d после исправлений получили те же856/299/4 и1098/416/4,
35/29.56 MHz PASS, TRACE37.258/30.457.592 words v11, два изменённых ASH
words и41 новый ASHC word; без новых FF/EBR. Ошибки эталона исправлены,
заранее разрешённые расхождения не используются. [Patch и проверки](eis-ashc.md).

AM4/microcpu имеют другой scope; сопоставимых ASHC CPI не найдено.
До дальнейшего EIS требуется экономия LUT: во FRAM scope осталось2 LUT
до желательных1100. MMU не добавляется и ресурсы под него не резервируются.


## CP23: измеренная экономия микросеквенсора

Сравнены три варианта при неизменных ISA, микрокоде и FRAM. Маски всех
источников next_address дали 917/1154 LUT core/FRAM и отклонены. Общий target
с low-bit OR-dispatch дал **850/1087 LUT** и выбран. Relaxed predicates дали
848/1090 LUT: меньший core не означал меньшего FRAM scope.

Относительно CP22 сэкономлены 6/11 LUT, FF/EBR сохранены: 299/416 FF и 4 EBR.
Выбранный TRACE 36.302/30.193 MHz, constraints 35/29.56 MHz PASS. Все 104
cycle/benchmark result files побайтно равны CP22; формальная проверка имеет
74 proven points и отвергает неправильный fault vector. Сохранены все шесть
raw fits; [подробности](area-sequencer.md), [manifest](verification-cp23.json).
MMU не добавляется; до желательных 1100 LUT у FRAM остаётся 13.


## CP24: XOR за пять микрокоманд

По DEC J-11 User Guide 6-44/C-6 и AM4 late-source sequencing добавлены XOR
во всех восьми destination modes, только comparator/dispatch в RTL и пять
слов microcode. RF/ALU/Q/PSW/sequencer/FRAM не менялись. Все 592 прежних слова
и 242 метки CP23 сохранены; всего 597 words/245 labels, encoding v11.

Измеренный core/FRAM fit — **854/1089 LUT**, **299/416 FF**, **4 EBR**.
Constraints 35/29.56 MHz PASS, TRACE **36.059/30.273 MHz**. Стоимость к CP23:
+4/+2 LUT, без новых FF/EBR. До желательных 1100 остаётся 11 LUT. Full board
peripherals и внешние pin delays исключены; 50 MHz не достигнуты.

Все 104 прежних cycle/benchmark result files побайтно равны CP23; 112 новых
portable/vendor result files совпали. Полный прогон: 242133 instruction cases,
41596 fault frames на каждое RAM/FRAM × ROM сочетание, 1018 benchmarks/ROM.
Независимая проверка охватывает 4698 XOR cases; восемь намеренных ошибок
отвергнуты. C executor для XOR не исправлялся. [Отчёт](eis-xor.md),
[manifest](verification-cp24.json), [benchmarks](benchmarks-cp24.json).

Уточнение старого описания CP22: сохранялись 235 меток CP21; число 551
обозначало микрокоманды. Подтверждено сборкой архивных исходников; старые
source archives и raw reports не изменялись. MMU отсутствует.


## CP25: MUL без расширения datapath

После изучения J-11 6-41, подробного DEC KE11-E/F flow и AM4 OR_MD/RAMQD
MUL выбран 16-step left-shift/add алгоритм на существующих RFQ_L/ADD/ADC.
45 новых слов/10 меток: 642×36 v11, 255 labels. Старые 597 слов/245 меток
сохранены. Единственное изменение RTL — opcode dispatch 062/066.

Diamond: core **841/299/4**, 35 MHz PASS/TRACE **36.926 MHz**;
FRAM/prefetch/IRQ+probe **1089/416/4**, 29.56 MHz PASS/TRACE **30.672 MHz**.
Это −13/0 LUT к CP24, без новых FF/EBR. Свободны 11 LUT до цели 1100,
191 до физического 1280; полный peripheral top не включён.

Исправлены две ошибки старого DCJ11 C MUL: чтение R до EA и продолжение
после operand fault. Старый C отрицательно проверен отдельным reproducer:
2368 из 4000 fault cases портили регистры/PSW после frame; после patch — 0.
Никакого исправления expected fixtures вручную нет.

Новые проверки: 13110 normal, 4000 fault, 7002 independent, 1024 CSR cases,
12 rejected mutations, 861968 arithmetic pairs. Полная регрессия:
255243 normal +45596 fault на каждом RAM/FRAM×portable/vendor; 1082 benchmarks
на ROM-модель. Все 120 results совпали; все 112 прежних results/22 fixtures
равны CP24. Проверены 104 synthesis archives/411 raw report hashes.

Register MUL benchmark: при FRAM+prefetch CPU/SPI 29.56/14.78 MHz полный loop
с MOV/BR даёт около 138486 MUL/s; интервал одного MUL — 156 тактов, ideal RAM
— 125. Эти результаты не означают 156 тактов для любой operand pair.
Сопоставимого AM4 MUL benchmark в доступных материалах нет; speedup не выдуман.
[Микрокод, документы, измерения](eis-mul.md), [manifest](verification-cp25.json).

## CP26 — DIV, 2026-09-09

58 новых слов, 700×36 v11; прежние 642 слова/255 меток сохранены.
CP26a **867 LUT/299 FF/4 EBR, TRACE 37.151 MHz, 35 MHz PASS**.
CP26b FRAM/prefetch/IRQ+probe **1099/416/4, TRACE 31.771 MHz, 29.56 MHz PASS**.
К CP25: +26/+10 LUT, 0 FF/EBR. До 1100 остался 1 LUT; полный board top
и external pin timing не включены. Дальше приоритет — площадь.

Исправлены DIV semantics в DCJ11 C profile; ошибки воспроизведены на
архивном CP25 C. 268907 normal +49596 fault cases на каждом memory/ROM
сочетании; 1146 benchmarks/ROM. Все 128 results равны, прежние 120 results
и 24 C fixtures побайтно сохранены. 106 archives/419 raw hashes проверены.

Representative RR DIV interval: 126 тактов ideal RAM, 157 FRAM+prefetch.
Полный loop с двумя MOV и BR: 97515 DIV/s при 29.56 MHz. Это
конкретные operands/loop, не постоянная latency. Сопоставимого AM4 DIV
benchmark нет, speedup не выдуман. [Детали](eis-div.md), [manifest](verification-cp26.json).

## CP27 — FIS, 2026-09-09

Повторно изучены AM4 FIS microcode/OR dispatch, старый microcpu FIS backend
и KE11-E/F User/Technical Manuals. Прежний microcpu FIS занимал 389 слов
вертикального 3584-word store; прямого сопоставимого FIS timing benchmark
AM4 нет. Его host/emulation expectations нельзя переносить без сверки:
DEC User Manual задаёт zero для underflow FADD/FSUB и trap для FMUL/FDIV.
uJ11 использует отдельный Fraction oracle и документирует отличие exact
rounded arithmetic от двух guard bits исторического KE11-F.

Получилось **223 новых слова +31 linking JUMP**, полный store **954×36 v12**;
700 старых words/271 label сохранены. Pair5=D/Q, остальные RTL state/memory
не меняются. CP27a **863/299/4, TRACE 35.954 MHz, 35 MHz PASS**;
CP27b **1095/416/4, TRACE 31.300 MHz, 29.56 MHz PASS**. −4 LUT в обоих fit
к CP26, без новых FF/EBR; это итоговый MAP delta, не изолированная цена FIS.

24 FIS benchmarks/ROM дают одинаковые RAM/FRAM clocks и SPI counters.
Типичный выбранный FMUL: 449 тактов ideal RAM, 1144 FRAM+prefetch;
полный MOV/MOV/MOV/FMUL/BR loop —14350 FMUL/s при nominal 29.56 MHz.
Эти данные не означают постоянной latency для любых operands.

AM4 PORTING-NOTES фиксирует физически проверенную сборку 2026-09-05:
1271 LUT/639 slices/7 EBR, RT-11 DIR и запись MACRO files. Это почти весь
HC1200. У uJ11 полный board top ещё не измерен: перенос периферии должен
иметь свой gate, а не оценку сложением отдельных modules. [FIS](fis.md),
[следующий integration gate](hc1200-integration.md). MMU отсутствует.


## CP28: первая полная плата uJ11 и RT-11

2026-09-10: исходный CP27 закоммичен как `0f1047e`. Интеграция FRAM,
KL11/KW11/panel/SD/RK/firmware сначала дала **1413 LUT/381 FF/5 EBR** —MAP fail.
Синхронный EBR dispatch, группировка ALU, компактный held-request FRAM и
точный LFSR timebase довели полный top до **1217 LUT/318 FF/6 EBR/610 slices**,
29.56 MHz PASS, TRACE **31.186 MHz**, полностью routed.
Неудачные read/next-address mux и промежуточные результаты сохранены в
[таблице CP28](synthesis.md); отсутствующие MAP/Fmax цифры не дописаны.

Cold RT-11/DIR прошёл через реальный SPI FRAM/SD и UART waveform:
355132188 clocks,3983731 retirements,98 файлов,3270 UART bytes,162 SD reads/6 writes.
Микрокод остался 954×36; существующий microasm11 собирает 426-byte bootstrap и
320-byte RK service в отдельный одно-EBR firmware ROM.

Документированный AM4 board от 2026-09-05 имел 1271 LUT/639 slices/7 EBR;
uJ11 CP28 экономит 54 LUT/29 slices/1 EBR. Сопоставимого AM4 cycle log нет,
преимущество скорости не заявляется. CP27b1095 LUT был неполным board scope
с prefetch/probe; CP28 prefetch пока выключен. Желательная площадь 900–1100 LUT,
полный FP11, external pin timing и физическая проверка uJ11 ещё впереди.
[Полные условия](hc1200-integration.md), [manifest](verification-cp28.json).


## CP30: начало FP11(A), 2026-09-10

Повторно проверены существующий `core/pdp11_fp.c`, AM4 `mc.asm` и
`/Users/sash/Work/FPGA/microcpu/ucode/j11_fis.asm`. В AM4/microcpu найден
FIS; готового полного FP11 в их текущих микропрограммах не найдено.
Существующий DCJ11 FP11 C core использован как независимый oracle только
для первого управляющего подмножества. Ни core, ни microasm11 не изменены.
Первичный ISA источник — DEC FP11-A User's Guide.

uJ11 CP30: общая память decoder/FP state, семь управляющих мнемоник,
33 новых слова, всего 987/1024×36. Полный top с FP: 1265 LUT4/327 FF/6 EBR,
635 slices, 31.284 MHz. Тот же RTL без FP: 1230 LUT4/326 FF/6 EBR,
618 slices, 30.116 MHz. Оба 29.56 MHz PASS. Первые три варианты не прошли
MAP: 1329/1275/1281 LUT, 668/643/644 slices. Это не fit полной FP11 ISA.
[Полные источники, raw reports, тесты и границы](fp11a.md).
