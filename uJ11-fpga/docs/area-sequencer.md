# CP23: общий адрес перехода микросеквенсора

**CP23 завершён: синтез, формальная проверка и полная регрессия прошли.** Выбран CP23c/d:
ядро с FRAM/prefetch, IRQ resolver и прежним probe занимает **1087 LUT**, на 11
меньше CP22. Один изменённый RTL module — `uj11_microseq.v`. ISA, микрокод
592×36 v11, RF 16×16, ALU/Q, decoder, PSW, memory/FRAM и C-эталон сохранены.
MMU отсутствует; адреса 16-битные.

## Три измеренных варианта

Diamond 3.14.0.75.2 / Synplify, LCMXO2-1200HC-4SG32C; отдельные свежие
implementation directories, прежние probes и ограничения 35/29.56 MHz.
Все шесть запусков прошли MAP/PAR/TRACE; выбор учитывает площадь FRAM scope.

| Вариант | Core LUT/FF/EBR | Core TRACE MHz | FRAM LUT/FF/EBR | FRAM TRACE MHz | Решение |
|---|---:|---:|---:|---:|---|
| CP22c/d, исходный | 856/299/4 | 37.258 | 1098/416/4 | 30.457 | Baseline |
| CP23a/b, независимые маски всех источников | 917/299/4 | 37.049 | 1154/416/4 | 30.842 | Отклонён: +56 LUT у FRAM |
| CP23c/d, общий target и low-bit OR-dispatch | 850/299/4 | 36.302 | 1087/416/4 | 30.193 | Выбран: −11 LUT у FRAM |
| CP23e/f, расширенные predicates OR-dispatch | 848/299/4 | 35.871 | 1090/416/4 | 30.450 | Отклонён: на 3 LUT больше выбранного FRAM |

Это результаты целых измеренных scopes, не отдельные оценки LUT микросеквенсора.
Явные маски не дали экономии: дополнительные enable terms и широкие OR buses
увеличили общий MAP count. У третьего варианта меньший core, но больший FRAM
scope. Его лучший TRACE Fmax не ускоряет benchmarks при прежних 29.56 MHz.
По приоритету площади выбран второй вариант.

Raw evidence: [CP23c](../synth/reports/cp23c/result.json),
[CP23d](../synth/reports/cp23d/result.json); все шесть source archives,
MAP/PAR/TRACE/Synplify reports сохранены в `synth/reports/cp23a..cp23f/`.

## Что изменено

Раньше control case повторно формировал полный 10-bit target для JUMP/TRAP,
CALL, READ/WRITE и пяти OR-dispatch операций. Теперь общий адрес задаётся как
`{target[9:3], target[2:0] | dispatch_bits}`; `dispatch_bits` равны нулю
в остальных control commands. Только conditional fallthrough, CALL overflow,
RETURN, opcode dispatch, STOP и WAIT переопределяют этот адрес.

ALU NEXT/PAGE/FETCH/FETCH_A1 и последовательность fault_redirect → fault_repair
→ reset сохранены. При fault target берётся без OR-dispatch modification.
Блок обновления uPC/link/link_valid побайтно прежний. Не добавлены FF, pipeline
stages, counters или ограничения на допустимые microinstructions.
Микрокод и все labels/ROM images не изменены; формат v11 остаётся прежним.

Вариант CP23e/f дополнительно упрощал predicates за счёт DISPATCH/RETURN/STOP,
которые переопределяют весь адрес. Он формально эквивалентен, но не выбран по
результату FRAM MAP. Эти relaxed predicates в текущий RTL не входят.

## Проверка эквивалентности

`tools/check_seq_cp23.py` извлекает настоящий старый RTL из frozen CP22c,
читает текущий RTL и запускает Yosys `equiv_make`, `equiv_simple`,
`equiv_induct -seq 4`, `equiv_status -assert`. Положительный результат:
**74 equivalence points proven, 0 unproven**. Намеренная замена repair vector
015→014 тем же инструментом оставляет один unproven bit и завершает проверку
с ошибкой. Ожидаемый failure сохранён отдельно от положительных test logs.

Сравниваются соответствующие состояния uPC/link/link_valid, в том числе
сброс; внешние inputs свободны. Это доказательство эквивалентности RTL
с двумя значениями битов, а не измерение аналоговых glitches или timing.
Сохранение state-update block вместе с эквивалентностью next-address logic
обеспечивает тот же переход состояния на каждом clock и при stall.

Использован Yosys 0.68, git 38e001a6f, через YoWASP. Точные версии пакетов:
[formal-requirements.txt](../tools/formal-requirements.txt). Cache лежит внутри
`build/yowasp-cache`; системные директории для формальной проверки не нужны.

```
python3 -m venv /tmp/uj11-formal
/tmp/uj11-formal/bin/pip install -r tools/formal-requirements.txt
make test-seq-equivalence YOSYS=/tmp/uj11-formal/bin/yowasp-yosys
make verify-cp23 YOSYS=/tmp/uj11-formal/bin/yowasp-yosys
```

Полная свежая регрессия прошла: **231105 instruction cases и 35836 fault frames**
на каждом RAM/FRAM × portable/vendor сочетании, **986 benchmarks на ROM-модель**.
Побайтно совпали с CP22 все **20 C fixture files, 40 instruction/fault cycle CSV
и 64 benchmark JSON**, включая microclocks, memory beats, SPI transactions и
SPI clocks. Дополнительно прошли прежние Python/ALU/RF/Q/sequencer/memory,
directed IRQ/trap/trace/FRAM-peripheral tests и общая C core regression.

Проверены 100 synthesis source archives и 395 raw report hashes; текущие
fit inputs совпадают с выбранными CP23c/d. [Manifest](verification-cp23.json),
[все benchmark/cycle totals](benchmarks-cp23.json).
Эталон и ISA semantics остаются исправленным CP22, включая ASH/ASHC ordering
и full 32-bit N/Z; прежние ошибочные snapshot semantics не возвращаются.

## Ресурсы и timing

CP23d MAP: **973 logic LUT + 48 distributed-RAM LUT + 66 ripple LUT = 1087**;
416 FF, 4 EBR, 545 SLICEs. До желательных 1100 осталось **13 LUT**, до физических
1280 — **193 LUT**. Предпочтительные 900–1000 LUT ещё не достигнуты.

TRACE Fmax снизился с 30.457 до **30.193 MHz**; constraint **29.56 MHz PASS**.
Худший путь имеет 32.665 ns, 18 logic levels, 58.4% routing; setup margin
**0.709 ns**, с учётом 0.173 ns skew и 0.282 ns CE_SET requirement.
Он идёт от EBR через register/address selection, FRAM match/ACK, bus fault/step
и RF/PC-write qualification к enable SPI transport `latched_wdata`.
Это новый фактический endpoint, не прежний CP22 путь к microstore address.
Изменение placement не позволяет приписать весь Fmax delta одной RTL операции.

При CPU/SPI 29.56/14.78 MHz все измеренные cycle counts остались прежними. Формат ROM,
число microinstructions, FRAM protocol и IRQ behavior не изменены.
Полные UART/timer/panel/SD/RK и внешние pin delays в fit не входят. 50 MHz
остаются целью; FPGA не программировалась. Следующее расширение EIS по-прежнему
требует собственного HC1200 gate и не может считать 13 LUT достаточным запасом
без измерения. MMU в эти этапы не входит и ресурсы под него не резервируются.
