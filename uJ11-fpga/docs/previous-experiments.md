# Предыдущие эксперименты: исходные данные uJ11

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
| `ucode` SD without FIS | 1041 | 423 | 7 | 41.943 | raw `impl1-sd-nofis`, [previous_reports.json](../history/README.md) |
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
| AM4 original adapter, minimal board | 903 | 285 | 7 | 29.574 | raw `original-am4-db546ca5`, [previous_reports.json](../history/README.md) |
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


Последующие checkpoints и первичные отчёты — в [истории](../history/README.md).
Текущие [ресурсы](synthesis.md) и [устройство](architecture.md) описаны отдельно.
