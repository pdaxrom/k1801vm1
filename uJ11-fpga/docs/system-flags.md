# CP18: condition-code operators, NOP и MFPT

Приняты CP18e/f. Адреса остаются 16-битными, logical == physical; MMU нет.
Профиль — один kernel register set, CM=PM=RS=0, NZVC/IPL/T. Полные J-11
processor modes и register banking этим checkpoint не реализуются.

## Поведение и первичный reference

Используется существующий [`core/core.c`](../../core/core.c): ветка
`Condition Code Operators` в `core_step` и `case 000007`. Исходник не изменён;
oracle вызывает настоящий DCJ11 executor с `ENABLE_MMU=0`. Access/vector hooks
вставляются только в проверяемую build-копию.

* Все 32 encodings octal `000240..000277`: `IR[3:0]` задаёт mask NZVC,
  `IR[4]=0` очищает выбранные bits, `IR[4]=1` устанавливает их. Остальные
  PSW bits сохраняются. Оба нулевых mask (`000240`, `000260`) — NOP.
* `MFPT` (`000007`) записывает **5** в R0 и сохраняет PSW.
* FETCH увеличивает PC на 2. После выполнения trace использует T, сохранённый
  при FETCH; trace имеет приоритет над IRQ. Stack frame содержит PSW уже
  после изменения NZVC. CC не меняет IPL/T, поэтому PSW LOAD и retirement
  допустимы на одном фронте. Это отличается от SPL, где нужен отдельный
  terminal cycle для проверки нового IPL.

## Микрокод и dispatch

Формат **v10, 36 bits** сохранён, microasm не расширялся. Все 452 старые
allocated words и все их label addresses неизменны. Теперь assembled **493**
words (48.14453125%): +41, из них четыре явных STOP для недостижимых OR_MD
веток совпадают с прежним ROM fill. В binary ROM изменились 37 адресов.

CC entry = `0x104 | (IR[3:0] << 3)`: две команды располагаются в каждом
из свободных слотов `104/10c/.../17c`. Это только прямые провода IR→uaddress;
opcode dispatch[9] остаётся нулём. Первое слово загружает mask в T4, второе
выполняет существующий `OR_MD` с base210. У CC `IR[5]=1`, поэтому
`IR[5:3]` даёт только 4/5 (clear) либо 6/7 (set). Leaves214/215 выполняют
`BIC(PSW,T4)`, leaves216/217 — `OR(PSW,T4)`, с flags=LOAD и seq=FETCH.

MFPT entry017 — `PASSA, pair=DA, d=IMM, imm=5, b=R0, dst=RF, seq=FETCH`.
Нечётный immediate не должен включать RTI/RTT trace policy: существующая v10
логика проверяет D≠IMM перед интерпретацией bit0 как `trace=RETURN`.

В RTL меняется только `uj11_decode.v`. RF16×16, Q, ALU, engine, PSW,
microsequencer, memory interface, FRAM transport/prefetch и IRQ adapter
побитно совпадают с CP17. Новых state registers нет.

## Три измеренных размещения

Все измерения — Diamond 3.14.0.75.2 MAP/PAR/TRACE, HC1200-4SG32C,
одинаковые scopes/constraints. Все шесть timing runs проходят.

| Вариант | Core LUT/FF/EBR | Core TRACE MHz | FRAM+IRQ LUT/FF/EBR | FRAM TRACE MHz | Решение |
|---|---|---:|---|---:|---|
| CP17 baseline | 826/297/4 | 36.302 | 1046/414/4 | 30.194 | Исторический |
| CP18a/b: таблица2e0, contiguous | 842/297/4 | 36.157 | 1083/414/4 | 30.893 | Сохранён для сравнения |
| CP18c/d: page1, разнесённые IR bits | 820/297/4 | 36.922 | 1134/416/4 | 30.785 | Отклонён: FRAM выше1100 LUT |
| **CP18e/f: page1, stride8** | **843/297/4** | **35.674** | **1062/414/4** | **30.409** | **Принят по FRAM area** |

Первый перенос убрал переменный dispatch[9] и уменьшил core, но ухудшил
полный FRAM synthesis. Это опровергает выбор по одному isolated core fit.
Конкретную стоимость всей разницы нельзя приписать одному bit: global mapping
меняется вместе с dispatch wiring. Во втором переносе IR mask образует один
непрерывный фрагмент address[6:3]. ROM width, clocks и state не менялись.
Архивы всех вариантов, включая отвергнутый, сохранены в `synth/reports`.

Final core: **35 MHz PASS**, critical path EBR→ALU/flags→PSW.Z,
27.693 ns, 14 levels, slack0.539 ns. Final FRAM: **29.56 MHz PASS**,
EBR→register/address selection→I/O read-data mux→MFPT decode→next uaddress→EBR,
32.911 ns, 17 levels, slack0.944 ns. 50 MHz пока не достигнуты.

FRAM scope включает serial probe и KW11/KL11 resolver, оставляет **218 LUT**
и **3 EBR** относительно device capacity. Полные UART/timer/panel/SD/RK и
внешние pin timings не входят; это не гарантия fit всей платы.

## Проверки

`system_flags_vectors.c` использует общий audited DCJ11 oracle. **21120 новых
completed cases, без exclusions**: все encodings × все 256 NZVC/T/IPL states
с IRQ/без IRQ; затем полная IPL×IRQ-priority matrix при обоих T. Проверяются
R0..R7, весь PSW, точный порядок/данные bus beats, stack writes, PC, retirement
и IRQ acknowledge. RAM имеет 0..3 waits; второй backend — реальный SPI FRAM
model и production prefetch/transport. Обе ROM-модели — portable и vendor DP8KC.

Новый fixture: RAM421120 clocks, FRAM9099624 clocks, 75504 logical bus beats
на каждый backend. **157285 completed DCJ11 cases** на RAM/FRAM×ROM pair:
136165 прежних +21120 новых. Отдельно 32780 прежних fault-frame cases.
Все прежние fixtures/per-case cycles сохранены. Старые exclusions и boundary
ограничения из CP16/CP17 остаются в силе; они не превращены в completed ISA.

Exhaustive decoder miter сравнивает все65536 encodings с archived CP17a:
изменяются только 33 новых opcodes. Теперь accepted56302. Из fallback oracle
убраны именно эти33 encodings (66 initial states):4160 completed из18468,
14308 excluded. Сам accepted reserved-case fixture не изменился.

Четыре negative controls требуют фактического failure: archived CP17 без CC;
clear, ошибочно устанавливающий flags; set, ошибочно очищающий flags;
неправильный MFPT ID. В логах сохраняются реальные architectural/bus mismatches.
При повторном использовании trace testbench Icarus обнаружил ошибку формирования
имени короткого fixture через packed ternary strings. Исправлен только TB:
используется явная string variable; обе suites проверены на final source.

**766 benchmark runs на ROM**, все746 прежних counts неизменны. CC/NOP —
3 execution microclocks, с ideal FETCH **4**; MFPT — 1 execution, всего **2**.
Обычный RR path по-прежнему 2 CPI ideal RAM и 39.078125 CPI в прежнем FRAM loop.
Новые unrolled CC/MFPT loops имеют другую длину; их CPI нельзя сравнивать
с прежним RR loop без учёта BR и SPI stream restart. Подробные counts:
[benchmarks-cp18.json](benchmarks-cp18.json), [verification-cp18.json](verification-cp18.json).

## Следующий gate

MFPS/MTPS — отдельный небольшой шаг с byte EA, MOVB sign extension,
сохранением T и IRQ arbitration после изменения IPL. HALT требует специального
stack/vector порядка DCJ11; RESET — отдельного peripheral-reset signal,
как в [`lsi11-fpga`](../reference/lsi11/am4_cpu11_bus.v), без сброса собственного
core и обрыва FRAM WRITE. Они пока не реализованы. EIS и полный board top
следуют отдельными measured gates. MMU в эти этапы не входит.
