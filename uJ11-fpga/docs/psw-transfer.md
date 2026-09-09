# CP19: MFPS и MTPS

CP19 добавляет MFPS (`106700..106777`) и MTPS (`106400..106477`, octal)
в текущий профиль: один kernel register set, CM=PM=RS=0, NZVC/IPL/T,
16-bit logical == physical address. MMU отсутствует. Синтез и полная
portable/vendor регрессия завершены: CP19 принят.

## Семантика и микрокод

Источник для differential testing — существующий DCJ11 executor
[`core/core.c`](../../core/core.c), cases `01064` и `01067`. Оригинал не менялся;
временная копия содержит только проверяемые audit hooks. Адреса операндов,
flags, выбор вектора и stack frame вычисляет сам reference.

MFPS передаёт младший байт исходного PSW. В регистре результат знаково
расширяется, как MOVB; memory destination получает один byte. N/Z относятся
к переданному byte, V очищается, C сохраняется. Особенности byte EA для SP/PC
выполняет прежний микрокод адресации. MFPS в память обновляет flags только
после успешного WRITE. MFPS в R7 использует обычный PC write/invalidation.

MTPS читает byte operand. Для текущего kernel profile:

```
new_PSW = (old_PSW & 0xff10) | (operand & 0x00ef)
```

T и старшие bits сохраняются; младшие NZVC/IPL загружаются. Это не реализация
mode/bank switching и не обещание privileged behavior вне CM=PM=RS=0.
T для trace по-прежнему захватывается на FETCH. PSW LOAD и terminal FETCH
разделены: IRQ comparator на завершающем такте видит уже новый IPL.

Новые routines занимают **14 words**, всего **507/1024 × 36 bits**, v10:

| Адреса µstore, hex | Работа |
|---|---|
| `1d8` | MFPS register: `PASSA D=PSW`, `dst=MOV`, `flags=NZV`, retire |
| `1da..1dc` | MFPS memory: CALL destination EA, PSW→T0, JUMP общий byte WRITE |
| `1a8` | MTPS register: Rd→T2 |
| `1aa..1ac` | MTPS memory: CALL destination EA, byte READ, MDR→T2 |
| `194..199` | MTPS: mask→T4, AND operand, invert mask, AND PSW, OR/LOAD, retire |

Все прежние **493 allocated words и labels** сохранены. Ни datapath,
ни RF/Q/ALU, ни PSW port, ни sequencer/FRAM transport не расширены.
У MTPS нет специального hardware PSW mask/bypass. Единственный изменённый
RTL module — decoder: общий prefix для word MARK и byte MTPS, отдельный
prefix MFPS. Mode=0 выбирается reduction OR трёх bits; uPC bit9 остаётся 0.
Exhaustive RTL miter проверяет все 65536 opcodes: ровно 128 новых accepted
encodings, остальные entries совпадают с архивным CP18e. Всего 56430.

## Реальные ресурсы HC1200

Diamond 3.14.0.75.2 / Synplify, LCMXO2-1200HC-4SG32C,
MAP/PAR/TRACE на пользовательском сервере 192.168.1.108:

| Gate / scope | LUT4 | FF | EBR | Constraint | TRACE Fmax |
|---|---:|---:|---:|---|---:|
| [CP19a core + probe](../synth/reports/cp19a/result.json) | 844 | 297 | 4 | 35 MHz PASS | 36.426 MHz |
| [CP19b FRAM/prefetch + KW11/KL11 resolver + probe](../synth/reports/cp19b/result.json) | 1073 | 414 | 4 | 29.56 MHz PASS | 30.046 MHz |

От CP18e/f: **+1/+11 LUT**, FF/EBR неизменны. У FRAM scope остаются 207 LUT
и 3 EBR из физической ёмкости; это не оценка остатка полного board top.
UART/timer/panel/SD/RK logic и внешние pin delays здесь не измерены.
Первый вариант укладывается в желательный предел 1100 LUT; отдельный
hardware mask ради нескольких циклов редкой MTPS пока не оправдан.

Core critical path: EBR→register/input select→ALU→RF writeback,
27.728 ns,17 logic levels, margin 1.118 ns при 35 MHz.
FRAM critical path: EBR→address/prefetch match→ACK/fault/step→PC write→SPI
transport enable,32.827 ns,20 levels, margin 0.547 ns при 29.56 MHz.
Это реальные paths данного placement; 50 MHz пока не достигнуты.
Нельзя объяснять изменение Fmax только добавленным decoder term: mapping
и routing изменились во всём scope.

## Проверки и явные границы reference

Новая инструкция считается проверенной только если совпали R0..R7, PSW,
точный порядок и данные logical bus beats, memory writes, retire и IRQ ACK.
RAM slave варьирует ожидание `case_id % 4`; FRAM использует production
transport/prefetch и SPI model из lsi11-fpga. Нечётный byte разрешён;
нечётный pointer/extension word проверяется отдельно как address fault.

В normal/trace/IRQ fixture **12852 completed cases из 13056 candidates**:

* Все 256 low-PSW values для MFPS, register/even-byte/odd-byte/PC destination.
* Все 256 source bytes для MTPS ×8 исходных IPL ×2 T × IRQ absent/pending7.
  High byte источника заполнен и должен быть отброшен.
* Обе операции ×8 modes ×8 registers, representative PSW и IRQ priorities1..7.
* Immediate, absolute, positive/negative PC-relative, wrap, odd byte EA.

**204 candidates исключены**, их исходные регистры и reason mask сохранены
в CSV. Reasons пересекаются: abort104, other/multiple-vector98,
internal I/O117, odd external word0, stack-limit status10. Эти 204 случая
не входят в compatible count. MFPS→SP может установить низкий SP;
последующий trace/IRQ в actual DCJ11 тогда вызывает red/yellow stack logic.
Даже без `fAbort` yellow trap способен дать второй vector004. Его исключение
основано на actual `J11_CPUERR & 0000014`, а не на догадке о значении SP.
Stack-limit architecture этим checkpoint не реализуется.

Отдельный fault fixture содержит **1008 completed vector004 frames**:
912 ACK errors на каждом обнаруженном operand beat и 96 odd-word faults.
Все modes1..7, все registers, четыре исходных NZVC/IPL states с T=0.
Новых exclusions нет. Сравнение заканчивается после готового frame перед
первой handler instruction. Reference audit не обнаружил post-frame
register/PSW changes или дополнительных bus accesses в этой новой группе.
Ошибку ACK вводит logical slave перед FRAM: у самого SPI FRAM нет ACK/error.

| Новая группа | Cases | RAM microclocks | FRAM microclocks | Logical beats | Repair clocks |
|---|---:|---:|---:|---:|---:|
| MFPS/MTPS normal + trace/IRQ | 12852 | 355470 | 6370712 | 54492 | — |
| MFPS/MTPS vector004 entry | 1008 | 33840 | 615848 | 6336 | 112 |

Пять negative controls обязаны дать FATAL: архивный decoder без MFPS;
zero extension вместо sign extension; MTPS перезаписывает T; terminal LOAD
проверяет старый IPL; MFPS обновляет NZV перед failed WRITE. Мутации
применяются к сохранённым synthesis sources в отдельных временных копиях.

Полная регрессия:170137 completed instruction cases и 33788 fault-frame cases
на каждой RAM/FRAM×portable/vendor ROM. Все 13 прежних oracle fixtures и 26
cycle CSV совпадают с CP18.786 benchmarks на ROM-модель; все 766 прежних
counts сохранены.82 result files (30 cycle CSV+52 benchmark JSON) одинаковы
для portable/vendor. Проверены 84 synthesis archives и 331 raw report hashes;
текущие inputs побайтно совпадают с CP19a/b. [Manifest](verification-cp19.json).

## Циклы и FRAM

MFPS Rn:1 execution word, с ideal FETCH **2 clocks**. MTPS Rn: capture+6 apply
words, с ideal FETCH **8 clocks**. Benchmarks включают 31 однотипную instruction
и BR; один loop прогрева исключён, затем измеряются 8 loops (256 instructions).
Поэтому loop CPI MTPS ниже 8. Проверяются PC/SP, все остальные registers, PSW
и обе половины memory destination.

| Loop | Ideal RAM CPI | Legacy FRAM CPI | Sequential FRAM CPI | Prefetch FRAM CPI |
|---|---:|---:|---:|---:|
| MFPS R0 | 2 | 107 | 41.125 | 40.15625 |
| MTPS R0 | 7.8125 | 112.8125 | 46.9375 | 40.15625 |
| MFPS (R1) | 9.75 | 216.46875 | 216.46875 | 216.46875 |
| MTPS (R1) | 13.625 | 203.875 | 203.875 | 203.875 |
| MTPS #value | 14.59375 | 204.84375 | 204.84375 | 204.84375 |

Register loops имеют 1 logical beat/instruction, memory/immediate loops —
1.96875 с учётом BR. Prefetch скрывает дополнительные execution clocks MTPS
в register stream. При nominal29.56 MHz оба register loops дают расчётные
~0.736 M instructions/s. Это simulation, не измерение платы.

MTPS immediate сейчас проходит через общий destination EA и byte data READ
из T1; этот READ не маркируется instruction stream. Добавлять отдельный
fast path ради этой редкой операции до общей ISA/EIS baseline не выбрано.
Indexed displacement и deferred PC pointer читаются прежними stream routines.
FRAM memory loops по-прежнему ограничены SPI transactions; аппаратного
множителя, divider, barrel shifter и cache не добавлено.

## Воспроизведение

```
make verify-cp19
make test-psw-transfer test-psw-transfer-fault
make vendor-psw-transfer vendor-psw-transfer-fault LATTICE_SIM_DIR=build/vendor
make benchmark-psw-transfer vendor-psw-transfer-benchmark LATTICE_SIM_DIR=build/vendor
python3 tools/check_decode_cp19.py
python3 tools/check_psw_transfer_negative.py
```

`verify_cp19.py` сохраняет portable results перед vendor run, проверяет их
совпадение и сравнивает старые fixtures/benchmarks с CP18. Recorder проверяет
точные fit inputs, source archives и raw report hashes. Следующие gates:
HALT/RESET и остальные нужные system operations, затем EIS. MMU не входит.
