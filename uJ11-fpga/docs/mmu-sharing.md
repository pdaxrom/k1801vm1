# CP34 — стоимость общей ALU и временные регистры для MMU

CP34 измеряет два способа разделить ALU с будущим MMU и проверяет,
какие временные регистры свободны на границах доступа к памяти.
**Production CPU/board остаётся CP31c без подключённого MMU.**
Микрокод не изменён: 954/1024×36 v12; FPGA остаётся CP29a.

## Измерение на HC1200

| Вариант | Арифметика MMU | LUT4 | FF | EBR | Slices | TRACE MHz |
|---|---|---:|---:|---:|---:|---:|
| [CP34a](../synth/reports/cp34a/result.json) | Отдельные PAR add и PDR length subtract | 471 | 173 | 0 | 236 | 42.535 |
| [CP34b](../synth/reports/cp34b/result.json) | Оба вычисления через существующую ALU | 487 | 173 | 0 | 244 | 39.156 |
| [CP34c](../synth/reports/cp34c/result.json) | Только PAR add через ALU; отдельный length subtract | 466 | 173 | 0 | 233 | 41.315 |

Diamond 3.14, LCMXO2-1200HC-4SG32C, constraint 29.56 MHz: все три
MAP/PAR/TRACE завершились успешно. Каждый probe включает один и тот же
RF16×16, Q16, ALU, обычные источники/назначения и интерфейс заимствования
ALU. 55 stimulus + 102 observation FF принадлежат измерительному стенду;
остальные 16 FF — Q. RF реализован distributed RAM. Снимки исходников,
input hashes и исходные отчёты сохранены отдельно для каждого варианта.
В a/b ещё нет третьего варианта wrapper; c соответствует текущим исходникам.

**Полное разделение ALU отвергнуто:** +16 LUT и снижение Fmax против a.
Удаление арифметических цепочек не компенсировало выбор операндов,
operation, carry/byte control и изоляцию записей. В hierarchical reports
число CCU2D уменьшается 23 → 9, но ORCALUT4 растёт 377 → 422 и PFUMX
24 → 40. Это primitive counts Synplify, их нельзя напрямую складывать
с mapped LUT4.

Разделение только relocation даёт **−5 LUT, −3 slices**, при снижении
Fmax 42.535 → 41.315 MHz. Это небольшой локальный выигрыш; вариант c
оставлен экспериментом, в production datapath не перенесён. Ни a, ни b,
ни c не включают APR store, MMR, PA classification, abort/restart или
CPU sequencer интеграции. Вычитать эти 5 LUT из board fit и обещать
помещающийся MMU нельзя. Полный CP31c по-прежнему занимает
1252 LUT / 326 FF / 6 EBR / 628 slices / 30.917 MHz.

## Контракт заимствования ALU

`rtl/experimental/uj11_datapath_borrow.v` — экспериментальная копия
минимального datapath. В обычном режиме её видимые результаты совпадают
с production datapath. При `borrow=1`:

* relocation вычисляет `(PAR16 + VA[12:6]) mod 2^16`;
* length phase сравнивает `VA[12:6]` с `PDR[14:8]`, учитывая PDR.ED;
* RF/Q сохраняются даже при `enable=1`; reset сохраняет обычный приоритет;
* PSW отсутствует в этом probe; будущий caller обязан блокировать его update.

`uj11_mmu_dp_compare.v` задаёт одинаковую наблюдаемость всех вариантов.
Неиспользуемые normal result/writeback/NZVC во время borrow замаскированы
нулём; RF ports/Q и отсутствие write проверяются всегда. MMU arithmetic
outputs действительны только в соответствующей фазе. Здесь нет отдельного
hardware EA engine и нет изменений architectural registers.

Оба варианта b/c сравнены с a: по **16977381 циклу** Verilator и
**265701 циклу** Icarus. Перебраны все 128 block offsets × 65536 APR
patterns × две фазы, затем 200000 смешанных normal/borrow cycles с reset,
RF/Q writes и byte/carry controls. Четырёхзначный прогон проверяет X в
неиспользуемых CPU controls. Четыре build-only мутации — снятие RF hold,
снятие Q hold, неверный PLF и неверные VA bits — обнаружены тем же тестом.

## T5–T7 на границе памяти

`tools/audit_mmu_scratch.py` анализирует текущие ROM/dispatch images:
**прежние значения T5, T6 и T7 не читаются до перезаписи ни на одной
из 88 memory microinstructions** (FETCH/READ/WRITE). Это свойство текущего
микрокода, а не постоянная аппаратная гарантия. FIS/EIS используют эти
регистры внутри вычислительных циклов; на границах памяти они свободны.
Q, T0–T4 такой общей гарантии не имеют.

Анализ отслеживает T0–T7/Q и объединяет все ветви, opcode entries,
OR-dispatch alternatives, CALL return sites, IRQ/trace, reset и memory
fault continuations. Множества чтений вычислены по реальным полям v12.
Два отрицательных контроля добавляют прямое использование T7 и чтение
T7 через opcode dispatch; оба делают T7 живым в соответствующей точке.

`tools/check_mmu_scratch.py` дополняет этот граф проверкой реального CPU:
копии testbench подменяют T5–T7 на каждом падающем фронте во время
memory word, включая wait states. Затем исходные ISA/bus oracles
проверяют R0–R7, PSW, точные обращения к памяти, traps/IRQ и результаты FIS.
Все изменения выполняются только в `build/`; production RTL, ROM и
ожидания fixtures не меняются. Дополнительная порча живого T0 должна
привести к провалу FIS oracle на том же исполняемом файле.
Дополнительный `tools/check_mmu_scratch_coverage.py` запускает single/control/
extra/PSW/HALT fixtures. Вместе **21 suite, 272917 architectural cases,
3311955 подмен и все 88/88 memory uPC** прошли без нарушения исходных
ожиданий. Порча T0 обнаружена в FIS case 0 при записи результата.
Покрытие всех uPC не является перебором всех architectural states.
Сборщик нормализует разную ширину hex uPC у Icarus и Verilator;
исправленный сборщик проверен повторным полным прогоном.
Точные результаты и hashes: [manifest](verification-cp34.json).

## Следующий эксперимент

Проверить небольшую MMU routine с использованием T5–T7 и обычной ALU,
сначала отдельно от рабочего CPU, затем измерить вместе с sequencer/APR.
Существующий CALL link часто занят EA routine, поэтому автоматически
вложить MMU CALL в него нельзя. Требуется измерить отдельный возврат
к исходной memory microinstruction и восстановление её operands.

Дополнительно необходимо сохранить PSW, MDR, IR и Q; анализ T-registers
не разрешает их портить. Conditional branch сейчас читает PSW NZVC,
а READ меняет MDR — эти эффекты нужно учесть до написания MMU entry/exit.
Проверить freeze при ожидании APR, reset/fault recovery, однократный внешний
request и отсутствие повторного MMU entry на возврате. Новые слова должны
пройти microassembler overlap/range checks в оставшихся 70 словах ROM;
их достаточность ещё не доказана.

После измерения entry/return — MMR0/1/2/3, abort/restart, canonical PA22,
physical RK DMA и RT-11XM по [обязательным gates](mmu.md).
CP34 не является запуском XM или benchmark CPU с MMU.

## Воспроизведение

```sh
make test-mmu-dp-sharing MMU_DP_CANDIDATE=1
make test-mmu-dp-sharing MMU_DP_CANDIDATE=2
make test-mmu-dp-negative
make test-mmu-scratch
python3 tools/record_cp34.py
```

Два sharing-прогона используют общие build paths и запускаются последовательно.
Для нового synthesis используется `tools/checkpoint.py` с cp34a/b/c в новом
implementation directory. Historical snapshots не перезаписываются.
