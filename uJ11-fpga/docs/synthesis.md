# Synthesis checkpoints

## CP30 — FP11 control/state, полный HC1200 top

Первое FP-подмножество измерено до расширения арифметики:
**CP30d: 1265 LUT4 / 327 FF / 6 EBR / 635 slices, 31.284 MHz, 29.56 PASS**.
Тот же оптимизированный RTL с опцией FP выключенной: **CP30e: 1230 LUT4 /
326 FF / 6 EBR / 618 slices, 30.116 MHz, PASS**. Microstore 987/1024×36 v13.
CP30a/b/c не прошли MAP (1329/1275/1281 LUT и 668/643/644 slices); raw reports
сохранены. [Полная таблица, причины и ограничения](fp11a.md). Полный FP11
не реализован; на плате остаётся CP29. Запас CP30d — 15 LUT и 5 slices.


## CP29a — panel input synchronizers and physical programming

Clean full-board result: **1239 LUT4, 326 FF, 6 EBR, 621 slices, 30.609 MHz TRACE**, 29.56 MHz PASS, fully routed. Microcode remains 954×36. The exact JED was programmed with FLASH verify and boots RT-11 on the real board. [Reports and hardware evidence](board-bringup-cp29.md).

## CP28 — полный board top, 2026-09-10

**1217 LUT /318 FF /6 EBR /610 slices; 29.56 MHz PASS, TRACE 31.186 MHz.**
Синтез включает physical SG32 top, OSCH/reset, core с FIS, SPI FRAM,
KL11/KW11/panel/SD/RK и firmware. Prefetch в данном top выключен.
Финальные [source/report hashes](../synth/reports/cp28m/inputs.json),
[area breakdown](../synth/reports/cp28m/design.areasrr),
[PAR](../synth/reports/cp28m/design.par), [TRACE](../synth/reports/cp28m/design.twr).
Внешние pin delays не заданы; Fmax описывает внутренние constrained paths.

После каждого изменения запускался отдельный Diamond gate; MAP failures
сохраняют только реально доступные цифры. PAR/TRACE не подменяются оценкой.
Во всех вариантах microstore **954/1024×36**, без нового ISA/MMU RTL.

| Revision / input SHA | LUT4 | FF | EBR | Slices | TRACE MHz | 29.56 MHz gate | Изменение / решение |
|---|---:|---:|---:|---:|---:|---|---|
| [cp28a](../synth/reports/cp28a/result.json) / `3d6d9e1246b1` | 1413 | 381 | 5 | 710 | — | FAIL | Первый полный top; превышение capacity |
| [cp28b](../synth/reports/cp28b/result.json) / `2a74b4d1882a` | 1437 | 381 | 5 | 722 | — | FAIL | Параллельный read mux увеличил LUT; отклонён |
| [cp28c](../synth/reports/cp28c/result.json) / `87c72d77dc37` | 1391 | 381 | 5 | 701 | — | FAIL | Иерархический combinational decoder; всё ещё over-map |
| [cp28d](../synth/reports/cp28d/result.json) / `ed50bdab7f40` | 1328 | 382 | 6 | 670 | — | FAIL | Первый synchronous decoder EBR; over-map |
| [cp28e](../synth/reports/cp28e/result.json) / `8abf0db654ff` | — | — | — | — | — | FAIL | Синтаксис SV cast не принят Verilog frontend; цифр MAP нет |
| [cp28f](../synth/reports/cp28f/result.json) / `24cbf4393480` | 1315 | 318 | 6 | 661 | — | FAIL | Компактный FRAM + удалён firmware data latch |
| [cp28g](../synth/reports/cp28g/result.json) / `9d8130a0c016` | 1315 | 318 | 6 | 661 | — | FAIL | Synthesis target 29.56 вместо 50 MHz; без выигрыша |
| [cp28h](../synth/reports/cp28h/result.json) / `a5848e437c13` | 1266 | 318 | 6 | 637 | 32.366 | PASS | Группировка ALU: fit, но four-state reset issue; отклонён |
| [cp28i](../synth/reports/cp28i/result.json) / `0ac64cb9dbf5` | 1275 | 318 | 6 | 640 | 31.023 | PASS | Исправлен four-state ALU; 640/640 slices, недостаточный запас |
| [cp28j](../synth/reports/cp28j/result.json) / `47c60cb8c313` | 1251 | 318 | 6 | 630 | 31.701 | PASS | Все dispatch entries в EBR; малый запас |
| [cp28k](../synth/reports/cp28k/result.json) / `fe29136feac8` | 1317 | 318 | 6 | 661 | — | FAIL | Параллельный next-address mux: +66 LUT; отклонён |
| [cp28l](../synth/reports/cp28l/result.json) / `80bfae512f7d` | 1217 | 318 | 6 | 610 | 31.186 | PASS | Точный LFSR KW11 timebase; функциональный baseline |
| [cp28m](../synth/reports/cp28m/result.json) / `e4d5e8680e16` | 1217 | 318 | 6 | 610 | 31.186 | PASS | Итоговый повтор, area report включён в hashes |

Это сокращение на 196 LUT от первого полного top. До физического лимита
остаётся 63 LUT/30 slices/1 EBR. **Предпочтительный предел 1100 LUT не достигнут.**
CP27b=1095 LUT включал иной scope (prefetch/probe, без полной периферии),
поэтому его разница с CP28 не измеряет стоимость одного блока.
Полный FP11 и его fit пока не реализованы. [Архитектура и verification](hc1200-integration.md).

**CP27a/b FIS: оба fit прошли.** Core **863/299/4**, 35 MHz PASS,
TRACE **35.954 MHz**; FRAM/prefetch/IRQ+probe **1095/416/4**,
29.56 MHz PASS, TRACE **31.300 MHz**. 954×36 v12: +254 слова и −4 LUT
в обоих mapped designs к CP26, без новых FF/EBR. Свободны 185 LUT до
физического 1280, но только 5 до желательных 1100. Полные UART/timer/
panel/SD/RK и pin timing не включены. [FIS и методика проверки](fis.md).

**CP26a/b DIV: synthesis и полная регрессия прошли.**
Core **867/299/4**, 35 MHz PASS, TRACE **37.151 MHz**;
FRAM/prefetch/IRQ+probe **1099/416/4**, 29.56 MHz PASS, TRACE **31.771 MHz**.
700×36 v11 (+58), +26/+10 LUT к CP25, без новых FF/EBR. До 1100 — 1 LUT.
Дальнейшее расширение требует снижения площади; full board top/pin timing
не включены. [DIV gate](eis-div.md), [manifest](verification-cp26.json).


Все результаты ниже — реальные Diamond **3.14.0.75.2 / Synplify**
Synthesis → Translate → MAP → PAR → TRACE на `sash@192.168.1.108`,
**LCMXO2-1200HC-4SG32C**, area strategy, no retiming, 2026-09-08/09.
Для `cp8c-retrace39` MAP/PAR взяты из родительского CP8c; повторён только TRACE.
Ни один failed run не представлен как проходящий 50 MHz.

**CP25a/b (MUL): synthesis и полная portable/vendor регрессия прошли.**
Core **841/299/4**, 35 MHz PASS, TRACE **36.926 MHz**;
FRAM/prefetch/IRQ+probe **1089/416/4**, 29.56 MHz PASS, TRACE **30.672 MHz**.
642 words v11 (+45), все 597 слов CP24 сохранены; без новых FF/EBR.
Core −13 LUT, FRAM LUT без изменения. До 1100 остаётся 11 LUT.
[Описание MUL и исправления C oracle](eis-mul.md).

**CP24a/b (XOR): синтез и полная portable/vendor регрессия прошли.**
Core **854/299/4**, 35 MHz PASS, TRACE **36.059 MHz**;
FRAM/prefetch/IRQ+probe **1089/416/4**, 29.56 MHz PASS, TRACE **30.273 MHz**.
597 words v11 (+5), все 592 слова CP23 сохранены. +4/+2 LUT, без новых FF/EBR.
До желательных 1100 остаётся 11 LUT. Все 104 прежних cycle/benchmark files
побайтно равны CP23. [XOR checkpoint](eis-xor.md), [manifest](verification-cp24.json).

**CP23c/d: общий target микросеквенсора:** core **850/299/4**, 35 MHz PASS,
TRACE **36.302 MHz**; FRAM/prefetch/IRQ+probe **1087/416/4**, 29.56 MHz PASS,
TRACE **30.193 MHz**. −6/−11 LUT к CP22, без новых FF/EBR. Полная регрессия
и формальная проверка прошли; все 104 result files побайтно равны CP22.
[Area experiment](area-sequencer.md), [manifest](verification-cp23.json).

**CP22c/d (исправленные ASH/ASHC) прошли fit:** core+probe **856/299/4**, **35 MHz PASS**,
TRACE **37.258 MHz**; FRAM/prefetch+IRQ resolver+probe **1098/416/4**,
**29.56 MHz PASS**, TRACE **30.457 MHz**.592 words v11, +41 words;
+7/+4 LUT к CP21, без новых FF/EBR. До желательных1100 осталось2 LUT.
[ASHC gate и границы oracle](eis-ashc.md).

Исторический CP21a/b (ASH): core+probe **849/299/4**, **35 MHz PASS**, TRACE **36.876 MHz**;
FRAM/prefetch+IRQ resolver+probe **1094/416/4**, **29.56 MHz PASS**, TRACE **31.309 MHz**.
551 words v11, +30 words; +13/+9 LUT к CP20, без новых FF/EBR.
До желательных1100 осталось6 LUT. Полная portable/vendor регрессия CP21 прошла.
[ASH gate](eis-ash.md).

Исторический CP20c/d: core+generic IRQ+probe **836/299/4**, **35 MHz PASS**,
TRACE **36.647 MHz**. FRAM/prefetch+KW11/KL11 IRQ resolver+probe
**1085/416/4**, **29.56 MHz PASS**, TRACE **30.593 MHz**.
Microcode **521 words, v11**. FF+2 к CP19: register выхода peripheral RESET
и observer этого выхода. CP20a/b отклонены из-за комбинационного init перед
asynchronous reset UART. До желательных1100 остаётся15 LUT в FRAM scope.
Полные UART/timer/panel/SD/RK и board pin delays сюда не входят;50 MHz остаётся
целью. [HALT/RESET](system-control.md).

Исторический CP9e/f: core 615/277/4, 40 MHz PASS, Fmax 40.925;
FRAM 910/393/4, 29.56 MHz PASS, Fmax 30.876, 342 слова v4.

Исторический CP8: FRAM 878/393/4, Fmax 31.540 MHz, 29.56 MHz PASS;
core 600/277/4, Fmax 39.955 MHz при 40 MHz FAIL. Его сохранённый placement
прошёл повторный TRACE при 39 MHz; это не новый MAP/PAR.

Исторический CP7: FRAM 861/393/4, Fmax 30.658 MHz, 29.56 PASS;
core 583/277/4, Fmax 41.810 MHz, 40 PASS. CP8 добавляет 17 LUT и сохраняет
FF/EBR обоих scopes. Exact assembler banner/docstring уточнён после synthesis;
executable AST и generated ROM совпадают с архивами (verification-cp8.json).

**Принят M0 v3: 582 LUT4, 277 FF, 4 EBR; 40 MHz PASS, Fmax 41.195 MHz.**
50 MHz остаётся целью: v3 при constraint 50 MHz даёт Fmax 42.902 MHz и
setup failure. Результаты при разных constraints имеют разный placement;
нельзя переносить Fmax одного placement на другой без нового TRACE.

## Измерения

LUT4 здесь — MAP total, включая distributed RAM и carry/ripple logic.
FF и LUT включают указанный probe. Words — явно assembled microinstructions,
а не физическая глубина ROM и не число execution clocks.

| Revision / input SHA-256 prefix | Features | LUT4 | FF | EBR | Fmax MHz | Constraint / result | Words | Notes |
|---|---|---:|---:|---:|---:|---|---:|---|
| [cp1](../synth/reports/cp1/inputs.json) / `b7d860d0a264` | ROM + sequencer, format v1 | 133 | 62 | 4 | 54.885 | 50 MHz / PASS | 45 | 41 harness FF; diagnostic image |
| [cp2](../synth/reports/cp2/inputs.json) / `db079aeaf5ce` | RF/Q/ALU, XOR before observation FF | 399 | 57 | 0 | 44.895 | 50 MHz / FAIL | — | Rejected probe: checksum in critical path |
| [cp2r1](../synth/reports/cp2r1/inputs.json) / `22cd87c1f03c` | RF/Q/ALU, raw observation FF | 382 | 135 | 0 | 53.453 | 50 MHz / PASS | — | Same datapath, corrected observer |
| [cp3](../synth/reports/cp3/inputs.json) / `393da452f8c5` | Engine + IR/PSW/MDR/memory, v1 | 661 | 303 | 4 | 36.682 | 50 MHz / FAIL | 23 | EBR→selector→RF→ALU→RF bottleneck |
| [cp3r1](../synth/reports/cp3r1/inputs.json) / `93c2985871de` | Parallel operand/writeback buses | 625 | 303 | 4 | 38.389 | 50 MHz / FAIL | 23 | −36 LUT4; keep change |
| [cp3r2](../synth/reports/cp3r2/inputs.json) / `3073ca340b14` | Aligned A/B fields, format v2 | 610 | 303 | 4 | 39.228 | 50 MHz / FAIL | 23 | −15 LUT4; keep change |
| [cp3r2-35](../synth/reports/cp3r2-35/inputs.json) / `f9bacc6b3ada` | Same v2 engine, 35 MHz constraint | 610 | 303 | 4 | 38.037 | 35 MHz / PASS | 23 | First integrated passing baseline |
| [cp4](../synth/reports/cp4/inputs.json) / `d2e79c663ea5` | Direct-bit M0 opcode decoder | 13 | 22 | 0 | 150.150 | 50 MHz / PASS | — | All 65536 encodings tested |
| [cp5-50](../synth/reports/cp5-50/inputs.json) / `156c84476ba5` | Full M0 v2, 50 MHz | 639 | 277 | 4 | 38.806 | 50 MHz / FAIL | 23 | 639 LUT baseline; timing fail |
| [cp5-35](../synth/reports/cp5-35/inputs.json) / `b60cedb16bb5` | Full M0 v2, 35 MHz | 639 | 277 | 4 | 36.642 | 35 MHz / PASS | 23 | setup/hold pass |
| [cp5v3-50](../synth/reports/cp5v3-50/inputs.json) / `2bbbef62d091` | FETCH uses ordinary ALU fields, v3 | 582 | 277 | 4 | 42.902 | 50 MHz / FAIL | 23 | −57 LUT4; +4.096 MHz vs v2 at 50 MHz |
| [cp5v3-40](../synth/reports/cp5v3-40/inputs.json) / `849914189671` | Final M0 v3, 40 MHz | 582 | 277 | 4 | 41.195 | 40 MHz / PASS | 23 | Accepted M0 baseline; margin 0.725 ns |
| [cp6a](../synth/reports/cp6a/inputs.json) / `cec99a4ab7c2` | M0 + unchanged legacy FRAM | 739 | 372 | 4 | 34.762 | 29.56 MHz / PASS | 23 | 107 CPI; demand-only I/O port |
| [cp6b](../synth/reports/cp6b/inputs.json) / `d86de2b9c41d` | Sequential READ, first revision | 789 | 390 | 4 | 31.444 | 29.56 MHz / PASS | 23 | 789 LUT gate before redirect timing change |
| [cp6c](../synth/reports/cp6c/inputs.json) / `924701480df0` | Sequential READ + prefetch, first revision | 824 | 392 | 4 | 28.065 | 29.56 MHz / FAIL | 23 | FAIL: ALU → PC comparison → SPI CS, 25 levels |
| [cp6c-r2](../synth/reports/cp6c-r2/inputs.json) / `c208d9be0007` | Prefetch, remove PC compare from SPI launch | 840 | 392 | 4 | 30.943 | 29.56 MHz / PASS | 23 | PASS; 840 LUT before stream READ qualifier |
| [cp6b-final](../synth/reports/cp6b-final/inputs.json) / `9af0a1f2637c` | Final sequential-only, opcode/extension stream | 801 | 390 | 4 | 31.219 | 29.56 MHz / PASS | 23 | MEMORY_MODE=1; 40.0625 CPI RR loop |
| [cp6c-final](../synth/reports/cp6c-final/inputs.json) / `021edcde8501` | Final prefetch, opcode/extension stream | 843 | 392 | 4 | 30.292 | 29.56 MHz / PASS | 23 | MEMORY_MODE=2; 39.078125 CPI; margin 0.817 ns |
| [cp7a](../synth/reports/cp7a/inputs.json) / `0f3b55ca1d2b` | Dynamic RS/RD stream hint, M0 image | 844 | 392 | 4 | 31.730 | 29.56 MHz / PASS | 23 | PC-only stream qualification before EA |
| [cp7b](../synth/reports/cp7b/inputs.json) / `f3ffde9f591f` | Word EA MOV/CMP/ADD, core + probe | 583 | 277 | 4 | 41.810 | 40 MHz / PASS | 143 | No RF/ALU/sequencer expansion; +1 LUT vs M0 |
| [cp7c](../synth/reports/cp7c/inputs.json) / `0b589506783a` | Word EA + FRAM, unrestricted prefetch | 825 | 392 | 4 | 31.581 | 29.56 MHz / PASS | 143 | Fit passes; rejected launch policy worsened memory CPI |
| [cp7d](../synth/reports/cp7d/inputs.json) / `f1f21bc73587` | Word EA + FRAM + microcode prefetch policy | 861 | 393 | 4 | 30.658 | 29.56 MHz / PASS | 143 | Accepted; +36 LUT/+1 FF vs cp7c; margin 1.211 ns |
| [cp7e](../synth/reports/cp7e/inputs.json) / `7ee2213cf92c` | Final CP7 core + probe | 583 | 277 | 4 | 41.810 | 40 MHz / PASS | 143 | Same datapath and fast RR execution; 40 MHz PASS |
| [cp8a](../synth/reports/cp8a/inputs.json) / `943c03238971` | v4 BA pair, CP7 ISA, core + probe | 600 | 277 | 4 | 40.287 | 40 MHz / PASS | 143 | BA gate before ISA expansion |
| [cp8b](../synth/reports/cp8b/inputs.json) / `b8085b387ade` | v4 BA pair, CP7 ISA + FRAM + probe | 850 | 393 | 4 | 29.871 | 29.56 MHz / PASS | 143 | BA gate before ISA expansion |
| [cp8c](../synth/reports/cp8c/inputs.json) / `f958462a25e2` | Seven word double-operand classes, core + probe | 600 | 277 | 4 | 39.955 | 40 MHz / FAIL | 214 | CP8 ISA gate |
| [cp8d](../synth/reports/cp8d/inputs.json) / `0c49d5060d12` | Seven word double-operand classes + FRAM + probe | 878 | 393 | 4 | 31.540 | 29.56 MHz / PASS | 214 | CP8 ISA gate |
| [cp8c-39](../synth/reports/cp8c-39/inputs.json) / `fe1a58be29e6` | Same CP8 RTL, new placement at 39 MHz | 600 | 277 | 4 | 38.724 | 39 MHz / FAIL | 214 | New routing worsened Fmax |
| [cp8c-retrace39](../synth/reports/cp8c-retrace39/inputs.json) / `fe1a58be29e6` | CP8c 40-MHz NCD, TRACE only at 39 MHz | 600 | 277 | 4 | 39.955 | 39 MHz / PASS | 214 | NCD preserved; not a new MAP/PAR |
| [cp9a](../synth/reports/cp9a/inputs.json) / `4607b0f65657` | 12 word unary classes, core + probe | 615 | 277 | 4 | 38.918 | 40 MHz / FAIL | 300 | CP9 gate |
| [cp9b](../synth/reports/cp9b/inputs.json) / `253a3be04ad7` | 12 word unary classes + FRAM + probe | 876 | 393 | 4 | 30.832 | 29.56 MHz / PASS | 300 | CP9 gate |
| [cp9c](../synth/reports/cp9c/inputs.json) / `08194775e228` | Word unary + all 15 branches, core + probe | 615 | 277 | 4 | 40.925 | 40 MHz / PASS | 342 | CP9 gate |
| [cp9d](../synth/reports/cp9d/inputs.json) / `8b09551a6ebe` | Word unary + all branches + FRAM + probe | 910 | 393 | 4 | 30.876 | 29.56 MHz / PASS | 342 | CP9 gate |
| [cp9e](../synth/reports/cp9e/inputs.json) / `5ec807ad224e` | Branch prefetch pause, final core + probe | 615 | 277 | 4 | 40.925 | 40 MHz / PASS | 342 | Accepted CP9 |
| [cp9f](../synth/reports/cp9f/inputs.json) / `bbce251489b2` | Branch prefetch pause, final core + FRAM + probe | 910 | 393 | 4 | 30.876 | 29.56 MHz / PASS | 342 | Accepted CP9 |
| [cp10a](../synth/reports/cp10a/inputs.json) / `78c5f8ea5fa9` | Byte RF/ALU probe | 434 | 138 | 0 | 52.348 | 50 MHz / PASS | — | CP10 gate |
| [cp10b](../synth/reports/cp10b/inputs.json) / `b3be384d1c9d` | MOVB core | 675 | 277 | 4 | 36.634 | 40 MHz / FAIL | 342 | CP10 gate |
| [cp10c](../synth/reports/cp10c/inputs.json) / `5cbfebc43584` | MOVB FRAM | 928 | 393 | 4 | 30.954 | 29.56 MHz / PASS | 342 | CP10 gate |
| [cp10d](../synth/reports/cp10d/inputs.json) / `c51086685040` | Parallel D core (rejected) | 680 | 277 | 4 | 37.533 | 40 MHz / FAIL | 342 | CP10 gate |
| [cp10e](../synth/reports/cp10e/inputs.json) / `1d442b6da8b1` | Parallel D FRAM (rejected) | 961 | 393 | 4 | 30.677 | 29.56 MHz / PASS | 342 | CP10 gate |
| [cp10f](../synth/reports/cp10f/inputs.json) / `2a395b740cda` | All byte core | 659 | 277 | 4 | 36.496 | 35 MHz / PASS | 342 | CP10 gate |
| [cp10g](../synth/reports/cp10g/inputs.json) / `9769d94b4192` | All byte FRAM, timing fail | 934 | 393 | 4 | 29.402 | 29.56 MHz / FAIL | 342 | CP10 gate |
| [cp10h](../synth/reports/cp10h/inputs.json) / `eb681857c24a` | Grouped PC compare core | 659 | 277 | 4 | 36.496 | 35 MHz / PASS | 342 | CP10 gate |
| [cp10i](../synth/reports/cp10i/inputs.json) / `8745da84db6b` | Grouped PC compare FRAM (rejected) | 935 | 393 | 4 | 29.025 | 29.56 MHz / FAIL | 342 | CP10 gate |
| [cp10j](../synth/reports/cp10j/inputs.json) / `1a32707cf3af` | Final byte core | 659 | 277 | 4 | 36.496 | 35 MHz / PASS | 342 | CP10 gate |
| [cp10k](../synth/reports/cp10k/inputs.json) / `1ce6414dbb3f` | Registered redirect, final FRAM | 905 | 394 | 4 | 32.047 | 29.56 MHz / PASS | 342 | CP10 gate |
| [cp11a](../synth/reports/cp11a/inputs.json) / `2f643df58a05` | Control ISA, microcoded SOB baseline | 673 | 277 | 4 | 36.817 | 35 MHz / PASS | 361 | v5 |
| [cp11b](../synth/reports/cp11b/inputs.json) / `c87249f2b527` | Control ISA + FRAM, baseline | 933 | 394 | 4 | 31.257 | 29.56 MHz / PASS | 361 | v5 |
| [cp11c](../synth/reports/cp11c/inputs.json) / `aa22b48e0f4d` | ALU-zero conditional retirement, rejected timing | 674 | 277 | 4 | 33.929 | 35 MHz / FAIL | 355 | v6 |
| [cp11d](../synth/reports/cp11d/inputs.json) / `9ca3e3844bf4` | ALU-zero conditional retirement + FRAM | 971 | 394 | 4 | 30.443 | 29.56 MHz / PASS | 355 | v6 |
| [cp11e](../synth/reports/cp11e/inputs.json) / `4b42fdaf5293` | Early A==1 retirement, final core | 694 | 277 | 4 | 36.358 | 35 MHz / PASS | 355 | v7 |
| [cp11f](../synth/reports/cp11f/inputs.json) / `72947aac35c0` | Early A==1 retirement, final FRAM | 970 | 394 | 4 | 31.672 | 29.56 MHz / PASS | 355 | v7 |
| [cp12a](../synth/reports/cp12a/inputs.json) / `8df8838659ad` | SWAB/SXT/MARK, priority decoder, core | 708 | 277 | 4 | 37.012 | 35 MHz / PASS | 393 | v7 |
| [cp12b](../synth/reports/cp12b/inputs.json) / `a1a7d7ca5718` | SWAB/SXT/MARK + FRAM, rejected area | 1081 | 394 | 4 | 30.650 | 29.56 MHz / PASS | 393 | v7 |
| [cp12c](../synth/reports/cp12c/inputs.json) / `7aa8190e619f` | Parallel class masks, accepted core | 708 | 277 | 4 | 35.723 | 35 MHz / PASS | 393 | v7 |
| [cp12d](../synth/reports/cp12d/inputs.json) / `e226025754d1` | Parallel class masks, accepted FRAM | 984 | 394 | 4 | 31.287 | 29.56 MHz / PASS | 393 | v7 |
| [cp13a](../synth/reports/cp13a/inputs.json) / `58a496fb1004` | Kernel/T=0 software traps/RTI, core | 735 | 277 | 4 | 36.340 | 35 MHz / PASS | 422 | v7 |
| [cp13b](../synth/reports/cp13b/inputs.json) / `1d31b8aaf457` | Kernel/T=0 software traps/RTI + FRAM | 999 | 394 | 4 | 31.245 | 29.56 MHz / PASS | 422 | v7 |
| [cp14a](../synth/reports/cp14a/inputs.json) / `ccad2da47806` | IRQ/WAIT/SPL core, generic inputs | 764 | 293 | 4 | 36.720 | 35 MHz / PASS | 446 | v8; 30 stimulus + 173 observation FF |
| [cp14b](../synth/reports/cp14b/inputs.json) / `e0e03ef9ccbb` | FRAM/prefetch, generic IRQ | 1038 | 410 | 4 | 30.992 | 29.56 MHz / PASS | 446 | All priority inputs dynamic |
| [cp14c](../synth/reports/cp14c/inputs.json) / `fed543dce403` | FRAM + KW11/KL11 IRQ resolver | 1016 | 410 | 4 | 31.221 | 29.56 MHz / PASS | 446 | BR4/BR6; peripheral CSR logic excluded |
| [cp14d](../synth/reports/cp14d/inputs.json) / `1d3d35d5a276` | Final core input snapshot | 764 | 293 | 4 | 36.720 | 35 MHz / PASS | 446 | Same RTL/result as cp14a; updated runner |
| [cp15a](../synth/reports/cp15a/inputs.json) / `03bbb64e8668` | Reserved/invalid-mode traps core | 772 | 293 | 4 | 36.476 | 35 MHz / PASS | 450 | Entries040/042; separate invalid-JMP mask |
| [cp15b](../synth/reports/cp15b/inputs.json) / `00029550b0a9` | Reserved traps + FRAM/resolver | 1049 | 410 | 4 | 31.944 | 29.56 MHz / PASS | 450 | +33 LUT against CP14c; investigated |
| [cp15c](../synth/reports/cp15c/inputs.json) / `8ad11c775379` | Upper entries, core | 779 | 293 | 4 | 35.828 | 35 MHz / PASS | 450 | 3fa/3fc; not selected |
| [cp15d](../synth/reports/cp15d/inputs.json) / `96cd99bd24f2` | Upper entries, FRAM/resolver | 1043 | 410 | 4 | 30.929 | 29.56 MHz / PASS | 450 | Only 6 LUT saved; reduced timing margin |
| [cp15e](../synth/reports/cp15e/inputs.json) / `d1965a963336` | Shared JMP predicate, core | 778 | 293 | 4 | 36.236 | 35 MHz / PASS | 450 | Final; exhaustive baseline equivalence |
| [cp15f](../synth/reports/cp15f/inputs.json) / `cf2c62e757c9` | Shared predicate, FRAM/resolver | 1036 | 410 | 4 | 31.697 | 29.56 MHz / PASS | 450 | Final; 13 LUT below cp15b |
| [cp16a](../synth/reports/cp16a/inputs.json) / `84673984ea08` | Memory fault redirect, core | 791 | 294 | 4 | 36.831 | 35 MHz / PASS | 452 | No fault_inc; rejected on abort state |
| [cp16b](../synth/reports/cp16b/inputs.json) / `dc4cd2ebd127` | Memory fault redirect, FRAM/resolver | 1062 | 413 | 4 | 31.083 | 29.56 MHz / PASS | 452 | No fault_inc; rejected on abort state |
| [cp16c](../synth/reports/cp16c/inputs.json) / `440061aeaef0` | Fault redirect + autoincrement repair, core | 792 | 295 | 4 | 36.988 | 35 MHz / PASS | 452 | Repair; early IRQ ordering bug, rejected |
| [cp16d](../synth/reports/cp16d/inputs.json) / `e841d4ec3c3c` | Fault repair + FRAM/prefetch/resolver | 1030 | 412 | 4 | 31.079 | 29.56 MHz / PASS | 452 | Repair; early IRQ ordering bug, rejected |
| [cp16e](../synth/reports/cp16e/inputs.json) / `e9807c1ceabf` | Defer fault IRQ until handler instruction, core | 809 | 296 | 4 | 36.552 | 35 MHz / PASS | 452 | Final; three new state FF |
| [cp16f](../synth/reports/cp16f/inputs.json) / `3b592e0d977c` | IRQ deferral + FRAM/prefetch/resolver | 1042 | 413 | 4 | 31.117 | 29.56 MHz / PASS | 452 | Final; three new state FF |
| [cp17a](../synth/reports/cp17a/inputs.json) / `9b863c6c4f26` | Trace/RTT core | 826 | 297 | 4 | 36.302 | 35 MHz / PASS | 452 | One T-snapshot FF, v10; 037 bit0 |
| [cp17b](../synth/reports/cp17b/inputs.json) / `53aa8835097c` | Trace/RTT + FRAM/prefetch/resolver | 1046 | 414 | 4 | 30.194 | 29.56 MHz / PASS | 452 | One T-snapshot FF, v10; 037 bit0 |
| [cp18a](../synth/reports/cp18a/inputs.json) / `3cad3de0d705` | CC/MFPT, contiguous page2 core | 842 | 297 | 4 | 36.157 | 35 MHz / PASS | 493 | Comparison; variable dispatch[9] |
| [cp18b](../synth/reports/cp18b/inputs.json) / `583e0e9bd6a8` | CC/MFPT, page2 FRAM/IRQ | 1083 | 414 | 4 | 30.893 | 29.56 MHz / PASS | 493 | Comparison, larger area |
| [cp18c](../synth/reports/cp18c/inputs.json) / `2a2cb83ba3f1` | CC/MFPT, scattered page1 core | 820 | 297 | 4 | 36.922 | 35 MHz / PASS | 493 | Core gain alone does not predict system fit |
| [cp18d](../synth/reports/cp18d/inputs.json) / `7eace453355c` | CC/MFPT, scattered page1 FRAM/IRQ | 1134 | 416 | 4 | 30.785 | 29.56 MHz / PASS | 493 | Rejected area:1134 LUT, timing passes |
| [cp18e](../synth/reports/cp18e/inputs.json) / `c3922167df72` | CC/MFPT, page1 stride8 core | 843 | 297 | 4 | 35.674 | 35 MHz / PASS | 493 | Accepted; unchanged RF/Q/ALU/state, v10 |
| [cp18f](../synth/reports/cp18f/inputs.json) / `8aa5cea918ce` | CC/MFPT, page1 stride8 FRAM/IRQ | 1062 | 414 | 4 | 30.409 | 29.56 MHz / PASS | 493 | Accepted by FRAM area; +16 LUT vs CP17 |
| [cp19a](../synth/reports/cp19a/inputs.json) / `a24db7a5ed94` | MFPS/MTPS, core + probe | 844 | 297 | 4 | 36.426 | 35 MHz / PASS | 507 | +1 LUT; same RF/Q/ALU/PSW state |
| [cp19b](../synth/reports/cp19b/inputs.json) / `aef9abdd0037` | MFPS/MTPS + FRAM/prefetch/IRQ resolver + probe | 1073 | 414 | 4 | 30.046 | 29.56 MHz / PASS | 507 | +11 LUT, no new FF/EBR; margin 0.547 ns |
| [cp20a](../synth/reports/cp20a/inputs.json) / `51f8d042e756` | HALT restart/RESET core + init observer | 832 | 298 | 4 | 36.516 | 35 MHz / PASS | 521 | Rejected: combinational init to async peripheral reset |
| [cp20b](../synth/reports/cp20b/inputs.json) / `3d63fe521343` | FRAM/prefetch/IRQ + peripheral reset | 1089 | 415 | 4 | 31.010 | 29.56 MHz / PASS | 521 | Rejected: combinational init to async peripheral reset |
| [cp20c](../synth/reports/cp20c/inputs.json) / `fb4cd648df12` | Registered init, core + probe | 836 | 299 | 4 | 36.647 | 35 MHz / PASS | 521 | Accepted v11; 1 output FF + 1 observation FF vs CP19 |
| [cp20d](../synth/reports/cp20d/inputs.json) / `285211dbb88a` | Registered init, FRAM/prefetch/IRQ + probe | 1085 | 416 | 4 | 30.593 | 29.56 MHz / PASS | 521 | Accepted v11; 1 output FF + 1 observation FF vs CP19 |
| [cp21a](../synth/reports/cp21a/inputs.json) / `84512116df66` | ASH core + probe | 849 | 299 | 4 | 36.876 | 35 MHz / PASS | 551 | Existing RF counter; no RTL state added |
| [cp21b](../synth/reports/cp21b/inputs.json) / `579d28c2222e` | ASH + FRAM/prefetch + IRQ resolver + probe | 1094 | 416 | 4 | 31.309 | 29.56 MHz / PASS | 551 | Existing RF counter; no RTL state added |
| [cp22a](../synth/reports/cp22a/inputs.json) / `ce2cd3993274` | ASHC core+IRQ+probe | 856 | 299 | 4 | 37.258 | 35.0 MHz / PASS | 592 | Rejected semantics: early snapshot, odd NZ after writeback |
| [cp22b](../synth/reports/cp22b/inputs.json) / `04da1a074b31` | ASHC FRAM/prefetch+IRQ+probe | 1098 | 416 | 4 | 30.457 | 29.56 MHz / PASS | 592 | Rejected semantics: early snapshot, odd NZ after writeback |
| [cp22c](../synth/reports/cp22c/inputs.json) / `61a8a8cebf43` | Corrected ASH/ASHC core+IRQ+probe | 856 | 299 | 4 | 37.258 | 35.0 MHz / PASS | 592 | count EA first; full32 NZ; no new state |
| [cp22d](../synth/reports/cp22d/inputs.json) / `a38c7ec6c179` | Corrected ASH/ASHC FRAM/prefetch+IRQ+probe | 1098 | 416 | 4 | 30.457 | 29.56 MHz / PASS | 592 | count EA first; full32 NZ; no new state |
| [cp23a](../synth/reports/cp23a/inputs.json) / `223361adfccf` | core+probe, shared target experiment | 917 | 299 | 4 | 37.049 | 35 MHz / PASS | 592 | Parallel address masks; rejected area |
| [cp23b](../synth/reports/cp23b/inputs.json) / `ab57374cb99f` | FRAM/prefetch/IRQ+probe, shared target experiment | 1154 | 416 | 4 | 30.842 | 29.56 MHz / PASS | 592 | Parallel address masks; rejected area >1100 |
| [cp23c](../synth/reports/cp23c/inputs.json) / `e8c943955f01` | core+probe, shared target experiment | 850 | 299 | 4 | 36.302 | 35 MHz / PASS | 592 | Shared target + low-bit OR; selected core |
| [cp23d](../synth/reports/cp23d/inputs.json) / `9bbb27d581b0` | FRAM/prefetch/IRQ+probe, shared target experiment | 1087 | 416 | 4 | 30.193 | 29.56 MHz / PASS | 592 | Shared target + low-bit OR; selected FRAM |
| [cp23e](../synth/reports/cp23e/inputs.json) / `f7f2695f47ef` | core+probe, shared target experiment | 848 | 299 | 4 | 35.871 | 35 MHz / PASS | 592 | Relaxed OR predicates; alternate core |
| [cp23f](../synth/reports/cp23f/inputs.json) / `c085b70b069c` | FRAM/prefetch/IRQ+probe, shared target experiment | 1090 | 416 | 4 | 30.450 | 29.56 MHz / PASS | 592 | Relaxed OR predicates; +3 LUT vs cp23d, rejected |
| [cp24a](../synth/reports/cp24a/inputs.json) / `5507053e7538` | XOR core, v11 | 854 | 299 | 4 | 36.059 | 35 MHz / PASS | 597 | +5 words; other RTL unchanged; full regression passed |
| [cp24b](../synth/reports/cp24b/inputs.json) / `bad7abdf9fb5` | XOR FRAM/prefetch/IRQ, v11 | 1089 | 416 | 4 | 30.273 | 29.56 MHz / PASS | 597 | +5 words; other RTL unchanged; full regression passed |
| [cp25a](../synth/reports/cp25a/inputs.json) / `1a72bce261e6` | MUL core, v11 | 841 | 299 | 4 | 36.926 | 35 MHz / PASS | 642 | +45 words; other RTL unchanged; full regression passed |
| [cp25b](../synth/reports/cp25b/inputs.json) / `68000586e225` | MUL FRAM/prefetch/IRQ, v11 | 1089 | 416 | 4 | 30.672 | 29.56 MHz / PASS | 642 | +45 words; other RTL unchanged; full regression passed |
| [cp26a](../synth/reports/cp26a/inputs.json) / `77f2dc7cea07` | DIV core, v11 | 867 | 299 | 4 | 37.151 | 35 MHz / PASS | 700 | +58 words; other RTL unchanged; full regression passed |
| [cp26b](../synth/reports/cp26b/inputs.json) / `f1cb4fe4bc45` | DIV FRAM/prefetch/IRQ, v11 | 1099 | 416 | 4 | 31.771 | 29.56 MHz / PASS | 700 | +58 words; other RTL unchanged; full regression passed |
| [cp27a](../synth/reports/cp27a/inputs.json) / `97746fe4ab2c` | FIS core, v12 D/Q | 863 | 299 | 4 | 35.954 | 35 MHz / PASS | 954 | +223 FIS words +31 bridges; old 700 words preserved |
| [cp27b](../synth/reports/cp27b/inputs.json) / `05342f308bd0` | FIS FRAM/prefetch/IRQ, v12 D/Q | 1095 | 416 | 4 | 31.300 | 29.56 MHz / PASS | 954 | −4 LUT vs CP26b; full board peripherals excluded |

Индекс с hashes: [synthesis-results.json](synthesis-results.json).
Каждый каталог `synth/reports/<revision>/` содержит `source.tgz`, `inputs.json`,
полные `design.mrp/.srr/.par/.twr` и build log; у всех успешно разобранных
runs есть первоначальный `result.json`. CP2 завершил toolchain, но старый
parser не распознал TRACE `Warning:` вместо `Report:`; его действительные
цифры извлечены из сохранённых raw reports исправленным parser. Все 35
source archives проверены против input hashes. Generated ROM включён в archives.

## Что потребляло ресурсы и ограничивало частоту

CP1: 40 stimulus FF + 1 checksum FF, 21 sequencer FF = 62 FF. Его 54.885 MHz
ограничивались путём ROM→predicate/next→checksum FF; это не bare ROM limit.
Actual ROM count — 4 EBR; vendor sweep всех слов и hold/feedback tests прошли.

CP2: сначала длинный XOR стоял после datapath перед observation FF. Он
искусственно ограничил probe до 44.895 MHz. После регистрации raw outputs
XOR вынесен за эти FF: 53.453 MHz, 382 LUT4. RF при этом не менялся.
Тестовая обвязка объявляет 36 input + 85 observation bits, Q — 16;
часть одинаковых observation bits была объединена, MAP показывает 135 FF.

CP3: интеграция добавила EBR clock-to-output (4.979 ns в critical path),
выбор literal/dynamic RF и control mux. 27.536 ns path в первой версии:
EBR→A field mux→dynamic selector→RF→operand selection→ALU→writeback→RF.
Реальный regression исследован до добавления opcode decoder.

Параллельные masked buses дали 661→625 LUT4; одинаковые позиции A/B в
ALU/control words — 625→610. Последний вариант прошёл отдельный fit 35 MHz.
В v2 core критическим стал EBR→FETCH override→operand mux→adder→Z→PSW.
В v3 сам FETCH кодирует ADD/AD/RF/TWO: override operation/pair/D устранены.
Это дало **639→582 LUT4** и **38.806→42.902 MHz** при том же 50 MHz target,
без изменения microcode word count, architectural state или CPI.

## Исторический M0 scope и ограничения измерения

`uj11_probe_core`: clk/reset/serial_in/serial_out, 18 input и 171 observation
FF, raw outputs регистрируются до XOR. Это делает пути через core наблюдаемыми
на SG32 без pinout на сотню внешних сигналов. Engine имеет 88 state bits;
MAP total FF=277. RF — 4 DPR16X4C + 4 SPR16X4C, **48 MAP LUT4** distributed
RAM. Whole-probe MAP: 504 logic + 48 distributed RAM + 30 ripple = 582 LUT4,
4 EBR. Нет отдельного bare-core fitted LUT result: арифметическое вычитание
checksum LUT из общего MAP его не заменяет.

Финальный 40 MHz [TRACE](../synth/reports/cp5v3-40/design.twr):
setup errors=0, hold errors=0, cumulative negative slack=0; full routing.
Худший путь EBR lane2→dynamic selector/RF→ALU/writeback→RF, delay 24.550 ns,
18 logic levels, margin **0.725 ns**. 50 MHz variant ограничен путём к
PSW Z: delay 22.970 ns, violation **3.309 ns**.

Timing coverage финального probe **86.95%**. Внешние input/output delays
не заданы, reset/asynchronous paths исключены согласно LPF; internal
synchronous paths проверены. Это не board-level memory timing closure и
не физический тест частоты. Подключение платы с реальной SPI FRAM потребует
своих I/O constraints и повторного MAP/PAR/TRACE. Для M0 доступно 698 из 1280 LUT4
и 3 из 7 EBR относительно данного probe; они не зарезервированы под MMU.
M0 содержит лишь четыре ISA classes, поэтому fit всей будущей ISA не доказан.

## Оценка архитектурных гипотез по M0

* **RF16×16:** дешёвая distributed RAM и нужный один execution cycle
  подтверждены. Причины переходить к 24/32-word multiport RF нет.
  FF/EBR RF alternative пока не синтезировалась: глобальная оптимальность
  RF не доказана, но измеренный вариант подходит для продолжения.
* **ALU/Q:** shared carry chain и shift-by-one достаточны для M0;
  selectors/operand/writeback/flags, а не multiplier/divider, остаются
  timing bottleneck. EIS ещё не реализован и не входит в CPI.
* **36 bits:** физические четыре EBR подтверждены. Контекстное переиспользование
  полей помогает только при отсутствии дополнительных mux; v1→v3 это показал.
  Сокращать ширину до 32 при тех же четырёх EBR оснований нет.
* **Sequencer:** synchronous next-address feedback работает без sequencing
  bubbles, 21 state FF; page8 и full control targets сочетаются. Глубина CALL
  ограничена одним уровнем и может потребовать изменения для будущих routines.
* **Decoder:** isolated 13 LUT4/22 FF probe, 150.150 MHz; direct opcode bits
  эффективны для четырёх classes. Полную PLM/distributed ROM сравнить позже,
  когда ISA станет шире. Сейчас она не определяет critical path.

Это обоснованный baseline, а не доказательство максимально возможных 50 MHz.
CP6 далее измерил instruction-stream prefetch, CP7 — addressing modes.
Упрощение selector/flags path остаётся направлением оптимизации; добавление
microclock к каждой инструкции требует отдельного сравнения throughput.

## CP7: addressing modes и политика prefetch

Общие routines для word MOV/ADD/CMP используют 143 слова вместо 23 M0.
RF, ALU, Q, engine и microsequencer не расширялись; small opcode decoder
переключает RR/EA entry одним адресным битом. Дополнение `uj11_stream`
разрешает динамический READ stream только при выбранном R7. Первый такой
gate с M0 image: 844 LUT/392 FF/4 EBR, 31.730 MHz, 29.56 PASS.

Core с EA (`cp7b`): 583 LUT, 277 FF, 4 EBR, 41.810 MHz при 40 MHz constraint.
FRAM `cp7c` проходит 29.56 MHz с 825 LUT, но benchmarks выявили лишние
speculative reads перед operand access. Этот вариант сохранён как неудачный
performance experiment, хотя timing у него проходит.

Control bit4 теперь разрешает/подавляет speculation; ALU наследует policy
через один FF. `cp7d`: **861 LUT, 393 FF, 4 EBR, 30.658 MHz**, margin **1.211 ns**.
Цена относительно cp7c — **36 LUT и 1 FF** по реальному MAP, не предполагаемые
«несколько LUT». Это оправдано устранением 22–66 лишних CPU clocks на многих
memory instructions. Immediate→register частично теряет перекрытие; точные
потери/выигрыши есть в benchmarks. RR fast path не замедлился.

Final critical path: 32.163 ns, 17 levels, EBR→RF/ALU→prediction-valid.
TRACE coverage 88.51%; full routing и setup/hold пройдены. External pin timing,
oscillator tolerance и полная board периферия остаются отдельной работой.
Повторный bare-core probe после metadata (`cp7e`) подтвердил 583/277/4 и 41.810 MHz.

## Исторический CP6: timing и стоимость транспорта

CP6: последовательное чтение дало первый pass 789 LUT, но prefetch 824 LUT
не прошёл nominal 29.56 MHz: 35.176 ns / 25 logic levels от microstore через
RF/ALU/new-PC comparison до SPI CS, violation 1.802 ns. Расширение ISA было
отложено до устранения этого пути. PC comparison теперь меняет только
prediction-valid; SPI close использует registered flag, запуск prefetch
блокируется на любом PC write. Промежуточный pass: 840 LUT, 30.943 MHz.

Финальные одинаковые sources с READ stream qualifier: **801 LUT/390 FF**
sequential-only и **843 LUT/392 FF** с prefetch, по 4 EBR. Цена опции:
42 LUT и 2 FF; rdata транспорта служит PF_DATA без дублирования 16-bit register.
Включённый prefetch: **30.292 MHz**, margin **0.817 ns**, 32.557 ns path
EBR→RF/datapath→prediction-valid, 16 levels. UART/SD/panel/timer/RK отсутствуют
в этом scope; probe содержит 19 stimulus и 176 raw observation FF.
TRACE coverage **88.18%**; внешние board I/O delays и oscillator tolerance
не закрыты этим четырёхпиновым измерением. 29.56 MHz — nominal board clock,
не заявление достижения исходной цели 50 MHz или полного board timing closure.

Первый CP6b compile потребовал заменить SystemVerilog sized cast на обычную
Verilog part-select constant; до исправления MAP не запускался и цифр не было.

## Воспроизводимый запуск

Текущий CP9 (каждый checkpoint в свежем implementation directory):

```
make test
make benchmark benchmark-fram benchmark-ea benchmark-cp9
make vendor-fram vendor-ea vendor-single vendor-branch
make vendor-benchmark vendor-fram-benchmark vendor-ea-benchmark vendor-cp9-benchmark
make synthesis-fram                         # current cp9f
```

В свежей копии проекта на Linux с установленным Diamond:

```
make all
make vendor-test vendor-engine vendor-memory-engine vendor-core
make synthesis                 # CP9e at 40 MHz; recorded PASS
```

Для другого constraint использовать отдельную свежую копию:

```
make synthesis SYNTH_MHZ=50
```

Для отдельных текущих blocks: `python3 tools/checkpoint.py cp2`, `cp3`,
`cp4` или `cp5 --mhz 40`. Сначала `make microcode-m0` для CP3/CP5.
Исторический exact checkpoint воспроизводится из его source.tgz; текущие
assembler/RTL уже отличаются от v1/v2. `make synthesis-seq` — отдельный CP1.
Каждый runner отвергает существующую implementation directory, проверяет
actual clock preference и сохраняет новые source/ROM/report hashes.
`python3 tools/summarize_reports.py` перечитывает архивированные raw reports.
FPGA programming, Programmer и board image deployment не выполнялись.

## CP8: промежуточный gate пары BA

До расширения ISA пара operands DZ заменена на BA (encoding v4). Загрузки
D выполняются через PASSA/DA. Core + probe: 600 LUT4/277 FF/4 EBR,
40 MHz PASS, Fmax 40.287 MHz. С FRAM: 850/393/4, 29.56 MHz PASS,
Fmax 29.871 MHz. ALU и ширина microinstruction не менялись. Рост core
с CP7 583 до 600 LUT измерен до добавления BIT/BIC/BIS/SUB. Меньший FRAM
MAP total не переносится на bare core: это другая полная конфигурация.

## CP8: ISA, timing и повторный TRACE

Семь word classes занимают **214 words**. Core CP8c: 600/277/4,
**40 MHz FAIL**, Fmax 39.955 MHz. Worst path EBR lane2 → selector/RF →
operand/ALU → PSW Z: 24.689 ns, 17 levels; setup violation **0.028 ns**.
Новый MAP/PAR при 39 MHz дал Fmax 38.724 MHz и FAIL. Это иллюстрирует,
почему снижение constraint и новый placement не равны проверке прежнего NCD.

Исходный fully routed CP8c NCD сохранён без изменений и проверен TRACE
с новым PRF 39 MHz. **PASS**, Fmax 39.955 MHz, margin **0.613 ns**.
MAP/PAR здесь не повторялись; LUT/FF/EBR относятся к тому же размещению.
Архив `cp8c-retrace39` содержит NCD, PRF, Tcl, raw reports и placement hash.
Факт такого pass не обещает той же частоты для другого placement.

Главный FRAM gate CP8d: **878 LUT4, 393 FF, 4 EBR, 31.540 MHz**,
29.56 MHz PASS. Worst path EBR lane2 → RF/ALU → buffer_valid:
31.251 ns, 20 levels, margin **2.123 ns**, coverage **88.89%**.
Никаких новых state FF/EBR относительно CP7; прирост 17 LUT у полного
FRAM probe и 17 LUT у core probe. Это не полный board top или pin timing test.

Воспроизведение принятого TRACE выполняется в свежей копии, чтобы не
перезаписывать архивированный report:

```
cp -R synth/reports/cp8c-retrace39 /tmp/uj11-retrace39
LD_PRELOAD=/lib/x86_64-linux-gnu/libstdc++.so.6 "$HOME/.local/lscc/diamond/3.14/bin/lin64/diamondc" /tmp/uj11-retrace39/retrace.tcl
```

Исторический CP8 воспроизводить из его source.tgz. Текущие make targets
уже относятся к CP9: `synthesis-fram` — CP9f, `synthesis` — CP9e.

## CP9: промежуточный gate word unary

12 однооперандных word-классов, 300 microinstructions. RF/ALU/engine/
sequencer не изменены; decoder использует IR[11:6] для прямого entry.

* [cp9a](../synth/reports/cp9a/result.json): 615 LUT4 / 277 FF / 4 EBR, Fmax 38.918 MHz, 40.0 MHz FAIL.
* [cp9b](../synth/reports/cp9b/result.json): 876 LUT4 / 393 FF / 4 EBR, Fmax 30.832 MHz, 29.56 MHz PASS.

## CP9: unary + branch до изменения prefetch policy

342 microinstructions; 12 word unary + 15 branch classes, без новых datapath/
sequencer state. Существующие seven double-operand classes сохранены.

* [cp9c](../synth/reports/cp9c/result.json): 615 LUT4 / 277 FF / 4 EBR, Fmax 40.925 MHz, 40.0 MHz PASS.
* [cp9d](../synth/reports/cp9d/result.json): 910 LUT4 / 393 FF / 4 EBR, Fmax 30.876 MHz, 29.56 MHz PASS.

Промежуточный CP9a ограничен EBR→RF→ALU→PSW Z: 25.356 ns,
18 levels, violation 0.695 ns. Итоговый CP9c использует то же datapath RTL,
но другой decoder/ROM и placement: 24.096 ns, 19 levels, margin 0.565 ns,
coverage 87.46%. Улучшение Fmax не приписывается ускорению ALU.

У CP9d новый worst path: EBR → RF address → I/O-page/rdata selection →
incoming opcode dispatch → next microaddress → EBR. Delay 32.414 ns,
15 levels, margin 1.441 ns, coverage 89.22%. В этом scope decoder уже
входит в critical path; следующая оптимизация должна проверять весь путь
FETCH, а не только изолированную PLM. 370 LUT и 3 EBR остаются относительно
полного probe; бюджет полной периферии ещё предстоит измерить.

342 слова вместо 214 не добавили физических EBR. Unary register instructions
используют одну ALU microinstruction; branch predicates составлены из старых
CJUMP без дополнительного state или compound condition hardware. Рост LUT
измеряется для всего scope, не выводится арифметически из длины микрокода.

## CP9: принятая политика branch prefetch

CP9c/d прошли timing, но benchmarks показали лишнее speculation на taken
conditional branch: 144 CPI вместо 108 sequential-only для BNE self-loop.
CJUMP слова branch routines теперь задают `prefetch=0`; ALU continuation
наследует паузу, следующий FETCH возвращает разрешение. Никакого нового RTL.

Повторный полный MAP/PAR/TRACE **CP9e/f** сохранил resources и timing:
core **615/277/4, 40 MHz PASS, 40.925 MHz**; FRAM **910/393/4,
29.56 MHz PASS, 30.876 MHz**. Critical paths, margins и coverage совпали
с CP9c/d. BNE self-loop стал 108 CPI; countdown DEC/BNE — 72.352941 CPI
вместо 88.176471. Not-taken BNE loop потерял 1.9375 CPI. Все реальные
варианты и benchmark policy comparison сохранены; итог — CP9e/f.

## CP10: byte datapath gate

[CP10a](../synth/reports/cp10a/result.json): 434 LUT4, 138 FF, 0 EBR,
50 MHz PASS, Fmax 52.348 MHz. Scope — RF/Q/word+byte ALU и probe с 37
stimulus FF; это не весь core. RF16×16 и один 17-bit adder сохранены.
CP2r1 (382/135/0) предшествовал pair BA, поэтому разность с ним нельзя
приписывать только byte arithmetic. Полная интеграция измеряется отдельно.

## CP10: MOVB integration gate

342 words, encoding v5; MOVB использует общие EA и execution routines.

* [cp10b](../synth/reports/cp10b/result.json): 675 LUT / 277 FF / 4 EBR, Fmax 36.634 MHz, 40.0 MHz FAIL.
* [cp10c](../synth/reports/cp10c/result.json): 928 LUT / 393 FF / 4 EBR, Fmax 30.954 MHz, 29.56 MHz PASS.

## CP10: отвергнутый parallel D-input experiment

* [cp10d](../synth/reports/cp10d/result.json): 680 LUT / 277 FF / 4 EBR, Fmax 37.533 MHz, 40.0 MHz FAIL.
* [cp10e](../synth/reports/cp10e/result.json): 961 LUT / 393 FF / 4 EBR, Fmax 30.677 MHz, 29.56 MHz PASS.

MOVB core worst path: EBR→register selector→STEP/D→ALU→PSW Z,
26.958 ns, 16 levels, violation 2.297 ns at 40 MHz. Parallel masked D buses
улучшили core Fmax 36.634→37.533, но FRAM scope вырос 928→961 LUT,
Fmax снизился 30.954→30.677. Приоритет HC1200 area/FRAM throughput: вариант
отвергнут, исходный case D mux восстановлен. Ни один не проходит 40 MHz.

## CP10: полный byte ISA, первый gate

5 double-byte + 12 unary-byte classes, 342 words.

* [cp10f](../synth/reports/cp10f/result.json): 659 LUT / 277 FF / 4 EBR, Fmax 36.496 MHz, 35.0 MHz PASS.
* [cp10g](../synth/reports/cp10g/result.json): 934 LUT / 393 FF / 4 EBR, Fmax 29.402 MHz, 29.56 MHz FAIL.


## CP10: PC redirect timing, принятый вариант

CP10g: 33.556 ns / 20 levels, нарушение 0.182 ns: EBR→B selector→RF→ALU→
merged PC writeback→compare→prediction_valid CE. Разбиение comparison на
четыре группы не помогло: CP10i 935/393/4, 29.025 MHz, FAIL, 33.998 ns /
23 levels. CP10h core остался 659/277/4, 35 MHz PASS, 36.496 MHz.

CP10k регистрирует mismatch в одном FF и сразу маскирует valid outputs.
Следующий edge очищает stored valid bits; cycle counts не меняются.
**905/394/4, 29.56 MHz PASS, 32.047 MHz**. Новый worst path по-прежнему
EBR→datapath→PC comparison, но заканчивается прямо в redirected FF:
30.865 ns, 18 levels, margin 2.625 ns. CP10j core: **659/277/4,
35 MHz PASS, 36.496 MHz**. По сравнению с CP9 FRAM: −5 LUT, +1 FF;
по сравнению с failed CP10g: −29 LUT, +1 FF. Разница относится к полному
MAP/PAR scope, включая изменившуюся оптимизацию/разводку.

Относительно 1280 LUT остаются 375 LUT и 3 EBR. Это запас измеренного probe,
а не обещание fit полной периферии; ресурсы под MMU не резервируются.


## CP11: JMP/JSR/RTS/SOB baseline

361 words, encoding v5. Control instructions reuse destination EA. SOB uses
PSW save/restore and microcoded six-bit displacement extraction, without a
new datapath/sequencer feature. CP11a core+probe: 673 LUT/277 FF/4 EBR,
35 MHz PASS, TRACE 36.817 MHz. CP11b FRAM+probe: 933/394/4,
29.56 MHz PASS, TRACE 31.257 MHz. This is the baseline before SOB optimization.


## CP11: SOB conditional retirement

CP11c v6 проверяет ALU Z: core 674/277/4, 35 MHz FAIL, Fmax 33.929.
Worst path EBR→RF→ALU→Z→sequencer→EBR: 29.499 ns / 21 levels,
violation 0.902 ns. CP11d FRAM: 971/394/4, 29.56 MHz PASS, Fmax 30.443.

Принят CP11e/f v7: predicate RF[A]==1 вычисляется до decrement.
Core 694/277/4, 35 MHz PASS, 36.358 MHz; FRAM 970/394/4,
29.56 MHz PASS, 31.672 MHz. FF/EBR сохранены. SOB стал 2/3 CPI с ideal RAM,
вместо 8/9 у baseline. Все 4480 SOB oracle cases ускорились ровно на шесть
микротактов; прочие control cases не изменились. 355 microinstructions.

У FRAM scope остаются 310 LUT / 3 EBR; это probe без полной периферии.
[Control semantics, tests и exclusions](control-flow.md),
[verification manifest](verification-cp11.json). Failed v6 сохранён вместе
с baseline и финальным v7; прохождение FRAM не скрывает core timing fail.


## CP12: SWAB/SXT/MARK и стоимость decoder

Только decoder и 38 дополнительных microinstructions; RF/Q/ALU/PSW/seq/FRAM
RTL не менялись. CP12a core 708/277/4, 35 MHz PASS/37.012; CP12b FRAM
1081/394/4, 29.56 MHz PASS/30.650. +111 LUT FRAM против CP11 неприемлемы
для этого небольшого расширения. Рост виден уже в Synplify ORCALUT4
(853→964), не является только routing effect.

Непересекающиеся opcode classes выражены через parallel masks вместо
priority mux. Exhaustive decoder result сохранён на всех 65536 opcodes.
CP12c core: **708/277/4, 35 MHz PASS/35.723**; CP12d FRAM:
**984/394/4, 29.56 MHz PASS/31.287 MHz**. При одинаковых clock constraints
это −97 LUT у FRAM scope против cp12b, без изменения core LUT count.
Относительно CP11 +14 LUT у каждого scope, прежние FF/EBR. 393 words, v7.

У FRAM probe остаются 296 LUT / 3 EBR; периферия платы и pin timing ещё
не измерены вместе. [Microcode и verification](extra-instructions.md).


## CP13: software traps/RTI в kernel/T=0 profile

29 дополнительных microinstructions, без функционального изменения engine,
RF, ALU, PSW, sequencer или FRAM transport. CP13a core **735/277/4,
35 MHz PASS, 36.340 MHz**; CP13b FRAM **999/394/4, 29.56 MHz PASS,
31.245 MHz**. Это +27/+15 LUT относительно CP12, прежние FF/EBR.
422 words, v7. IRQ, trace, mode exchange и architectural bus/illegal traps
сюда ещё не входят. [Семантика и tests](software-traps.md).


## CP14: IRQ accounting, WAIT и SPL

Два новых core state FF, vector хранится в существующем MDR. Boundary IRQ
check не меняет ни один из 690 прежних benchmark counts. CP14a/d core:
764/293/4, 35 MHz PASS/36.720; CP14b generic FRAM: 1038/410/4,
29.56 MHz PASS/30.992. Реальный state и probe overhead разделены в
[interrupts.md](interrupts.md).

CP14c FRAM+KW11/KL11 resolver: 1016/410/4, 29.56 PASS/31.221.
Это отдельный scope с BR4/BR6 и меньшим количеством generic stimulus inputs;
его LUT difference нельзя называть стоимостью добавления adapter к CP14b.
Critical path остаётся EBR→D/ALU→prefetch redirected: 31.691 ns/22 levels,
slack 1.799 ns. Осталось 264 LUT/3 EBR в этом probe, не в полном board top.
446 words v8, 24 новых; все 422 CP13 words/labels сохранены.


## CP15: fallback vector dispatch

Прирост baseline FRAM 1049 против CP14c1016 заметен уже в Synplify:
ORCALUT4 899→933. Перенос microcode entries в 3fa/3fc даёт 1043 LUT, но core
растёт 772→779 и оба Fmax уменьшаются. Final shared JMP predicate с entries
040/042 даёт core 778/293/4, 35 PASS/36.236; FRAM 1036/410/4,
29.56 PASS/31.697. Все варианты и exact inputs сохранены.

Final FRAM critical path: EBR→RF/address selection→I/O data mux→decode/seq→EBR,
31.575 ns, 16 levels, slack 2.280 ns. Core: EBR→ALU→PSW.Z,
27.258 ns, 17 levels. Все старые 714 benchmark counts и 446 occupied words
сохранены; новых четыре words. [Semantics и verification](reserved-traps.md).

## CP16: abort redirect и проверенное восстановление autoincrement

Первый вариант прошёл timing, но отрицательный differential test показал
ошибку состояния source mode2/3 и destination mode3. CP16a/b сохранены как
отклонённый вариант: 791/294/4 и 1062/413/4. Добавлен READ bit2=fault_inc,
выполняющий один прежний ADD STEP/TWO перед vector004 при ошибке.
Успешный path и prefetch order не меняются; assembler проверяет continuation.
Промежуточные CP16c/d:792/295/4 и 1030/412/4. Снижение FRAM LUT — результат whole-design
mapping; оно не означает отрицательной аппаратной цены дополнительного FF.

Critical CP16d: EBR→RF address selection→I/O data mux→opcode/SPL dispatch→
sequencer→EBR, 32.202 ns,16 levels, slack1.653 ns при 29.56 MHz. ALU не входит
в этот worst path. Core CP16c: EBR→D selection/ALU→RF write data,
27.311 ns,17 levels, slack1.535 ns при 35 MHz. TRACE Fmax учитывает skew/setup,
поэтому не равен просто 1/path-delay.

452 words v9: два добавленных words, семь JUMP→TRAP и три fault_inc bits;
все прежние label addresses и остальные 440 words сохранены.
124969 обычных completed-instruction cases и 32780 fault-frame comparisons;
726 benchmarks/ROM. Полные исходные MAP/PAR/TRACE, inputs и source.tgz
сохранены. [Semantics, exclusions и measured scope](memory-faults.md).

Проверка unmasked IRQ выявила второй semantic gate: IRQ на выходе из fault
frame должен ждать первую handler instruction. CP16c/d отклонены по этому
сценарию. Один дополнительный state FF различает IRQ-frame и fault-frame.
Финальные CP16e/f: 809/296/4, 35 MHz PASS/36.552 и 1042/413/4,
29.56 MHz PASS/31.117. Восемь primary DCJ11 tests и 32 FRAM-system scenarios
подтверждают порядок при IPL0/IPL7; old native/IRQ/benchmark counts сохранены.
74 synthesis archives и 291 raw report hashes проверяются recorder.

Финальный critical CP16f: EBR→RF/D selection→ALU carry chain→writeback→
prefetch PC/tag comparison→redirected FF, 31.798 ns, 19 logic levels,
slack 1.692 ns. Путь через I/O/decoder→EBR близок: slack 1.713 ns.
CP16e worst path — EBR→D selection/ALU→PSW.Z, 27.019 ns,
16 levels, slack 1.213 ns. Обе ветви нуждаются в измерениях при дальнейшей
оптимизации: сводить final critical path только к decoder уже неверно.

## CP17: trace/RTT с одним новым FF

Core 826/297/4, 35 MHz PASS/36.302; FRAM+IRQ resolver 1046/414/4,
29.56 MHz PASS/30.194. Относительно CP16: +17 LUT/+1 FF core,
+4 LUT/+1 FF FRAM, 4 EBR без изменения. Microstore остаётся 452 words;
037 bit0 задаёт return trace policy, остальные 451 words и все labels сохранены.

Core critical: EBR→ALU/flags→PSW.Z, 27.208 ns, 14 levels, slack 1.024 ns.
FRAM critical: EBR→datapath→prefetch PC/tag compare→redirected FF,
32.780 ns, 21 levels, slack 0.710 ns. У CP16 этот slack был 1.692 ns;
номинальная частота 29.56 MHz пройдена, цель 50 MHz остаётся открытой.

В fit по-прежнему нет полного board top и external pin constraints.
234 свободных LUT/3 EBR не гарантируют размещения всей периферии.
76 synthesis archives и 299 raw report hashes сохраняются и проверяются.
[Trace/RTT semantics и verification](trace-rtt.md).

## CP18: CC/NOP/MFPT и стоимость размещения dispatch

Сравнены три варианта одной ISA и того же числа execution cycles. Приняты
CP18e/f:843/297/4 core,1062/414/4 FRAM+resolver. См. [все варианты](system-flags.md).
CP18a/b использует переменный dispatch[9]. CP18c/d возвращает constant-zero
bit9, но полный FRAM fit растёт до 1134 LUT/416 FF; он отвергнут по area,
хотя timing проходит. CP18e/f кладёт mask в непрерывный uaddress[6:3].
Нельзя приписать всю разницу одному биту: synthesis mapping меняется глобально.

Final core path EBR→flags→PSW.Z:27.693 ns/14 levels, slack0.539 ns при 35 MHz.
Final FRAM path EBR→RF/address→I/O mux→MFPT decode→uaddress→EBR:
32.911 ns/17 levels, slack0.944 ns при 29.56 MHz. Все старые RTL modules,
кроме decoder, побитно сохранены. Четыре EBR и ширина 36 bits не менялись.
[Exact-source audit](verification-cp18.json). Full-board fit остаётся отдельным gate.

## CP19: MFPS/MTPS

Две инструкции добавлены 14 words существующего encoding v10 и компактным
opcode dispatch. Сохранены все 493 прежних words/labels и весь RTL кроме decoder.
Core844 LUT (+1 от CP18), FRAM1073 (+11), одинаковые FF/EBR. Первый fit проходит
ресурсный предел 1100 LUT и исходные 35/29.56 MHz constraints.

Core EBR→select→ALU→RF:27.728 ns/17 levels, margin 1.118 ns. FRAM
EBR→address/prefetch hit→ACK/fault/step→PC write→transport enable:
32.827 ns/20 levels, margin 0.547 ns. Остаток 207 LUT/3 EBR относится только к
измеренному probe scope. Полный board fit пока не получен. Source archives
и MAP/PAR/TRACE/Synplify reports сохранены отдельно для обоих gates.

## CP20 HALT/RESET gate

CP20a/b функционально прошли, но комбинационный JUMP.init не подходит для
asynchronous reset входов actual KL11 UART: физический decode нескольких
ROM bits может давать краткие переходные импульсы. CP20c/d фиксируют init
в выходном FF; импульс покрывает существующий settling word, поэтому CPI
не меняется. Вся итоговая RTL повторно проверена portable/vendor моделями.

При одинаковых scopes и constraints относительно CP19:core−8 LUT,FRAM+12 LUT,
FF+2 (register init + observer). FRAM TRACE critical path EBR→prefetch.redirected
FF32.348 ns,20 logic levels,53.5% route. LUT delta отражает глобальный synthesis,
а не изолированную цену одного FF. Номинальные29.56 MHz проходят;30.593 — TRACE
этого placement, не измеренный board clock. Все четыре source archives и raw
reports сохранены; финальные inputs должны совпадать с CP20c/d побайтно.

## CP22: ASHC и исправление порядка операндов

CP22a/b отвергнуты по семантике, несмотря на passing timing: в C oracle
были ранний snapshot ASH/ASHC и ASHC N/Z после alias writeback. CP22c/d
содержат исправления. Ресурсы856/299/4 и1098/416/4 не изменились; TRACE
37.258/30.457 MHz при35/29.56 MHz PASS.

В CP22d worst path32.859ns,22 levels,54.6% route: EBR→register selector→
сравнение адреса FRAM→match/buffer→ACK/fault redirect→microstore address.
Участок сравнения реализован carry/ripple logic. По одним generated net names
нельзя считать его именно RTL-инкрементом following_word; цепь должна
исследоваться по mapped netlist при следующей оптимизации.

До желательных1100 LUT остаётся2. Следующий area/timing эксперимент должен
сохранить exact memory/IRQ/trace behavior и проверяться сначала на HC1200;
добавление EIS decoder без измерения не принимается. MMU не участвует.
[Алгоритм, исправленный эталон и измерения](eis-ashc.md).
