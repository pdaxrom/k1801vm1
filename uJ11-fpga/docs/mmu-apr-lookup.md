# CP37 — PAR/PDR из EBR в служебный микрокод

Работает экспериментальное чтение kernel unified PAR/PDR перед каждым
FETCH/READ/WRITE: данные поступают через общую ALU в T6/T7, PSW сохраняется
в T5 и восстанавливается перед возвратом. Все 954 прежние микрокоманды
сохранены побитно. Эксперимент занимает 963/1024×36 и добавляет 10 clocks
на memory word без внешнего hold. Production остаётся CP36f; плата — CP29a.

**Это нижняя оценка стоимости пути чтения APR, ещё не MMU.** Запись EBR
в этом board gate отключена. Контроллер программирования APR из CP33,
его CSR decode и арбитраж общего порта пока не включены. Верификация
с ненулевыми PAR/PDR загружает физическую модель памяти из testbench;
гостевая программа их пока не программирует. Проход через нулевые APR
в FIS/RT-11FB не проверяет трансляцию или права доступа.

## HC1200: варианты и подтверждение до дальнейшего расширения

| Revision | Вариант | LUT4 | FF | EBR | Slices | TRACE MHz | Итог |
|---|---|---:|---:|---:|---:|---:|---|
| CP36g | Только entry/return | 1243 | 338 | 6 | 626 | 31.107 | PASS |
| [CP37a](../synth/reports/cp37a/result.json) | Saved A4, RF mux, ready/wait | 1296 | 343 | 7 | 650 | — | MAP overflow |
| [CP37b](../synth/reports/cp37b/result.json) | Saved page3, ready/wait | 1300 | 342 | 7 | 654 | — | MAP overflow |
| [CP37c](../synth/reports/cp37c/result.json) | Page3, чтение без ready/wait | 1286 | 341 | 7 | 647 | — | MAP overflow |
| [CP37d](../synth/reports/cp37d/result.json) | То же, отдельное masked добавление к D | 1265 | 341 | 7 | 635 | 30.327 | MAP/PAR/TRACE PASS |
| [CP37e](../synth/reports/cp37e/result.json) | Финальный набор исходников, та же логика d | 1265 | 341 | 7 | 635 | 30.327 | MAP/PAR/TRACE PASS |

Diamond 3.14.0.75.2, LCMXO2-1200HC-4SG32C, constraint 29.56 MHz.
В каждом варианте сохранён полный board: FIS, firmware ROM, SPI FRAM,
SD/RK, KL11/HG, KW11, RGB/HDSP/keyboard, OSCH/reset/pins. A–C превышают
1280 LUT и 640 slices; для них Fmax не получен, нулём его не подменяем.
У d critical path EBR→логика управления→EBR: 33.000 ns, 18 levels,
40.6% logic / 59.4% route, slack 0.855 ns. External pin delays не заданы.

Финальный e подтверждает d после уточнения комментариев к вариантам;
RTL и ROM image идентичны. D/E относительно CP36g добавляют **22 LUT / 3 FF / 1 EBR / 9 slices**.
Осталось **15 LUT / 5 slices / 0 EBR**. Этот запас не доказывает размещения
CSR/MMR, трансляции, PDR checks, abort/restart и PA22/DMA. Эксперимент
не перенесён в production и не запрограммирован на плату.

## Что оказалось дорогим

Первый вариант сохранял decoded RF A4 и добавлял private selector24,
чтобы читать исходный VA через прежний RF. Замена на три бита VA[15:13]
не дала ожидаемой экономии общего MAP: 1296→1300 LUT, хотя убрала FF
и RF mux. Поэтому локальное число вентилей не использовалось как итог.

Ready FF и два wait clocks оказались лишними. На фронте APR_READ EBR
фиксирует адрес и выдаёт слово. На следующем фронте ALU save записывает
его в T6/T7. Microstore и APR EBR работают синхронно; отдельного ожидания
между этими двумя микрокомандами не требуется. Это уменьшило MAP до
1286 LUT и overhead с 12 до 10 clocks. CP33 read/write controller не
менялся: его собственный handshake по-прежнему занимает 2/3 clocks.

Последняя форма сохраняет исходный D selector с ZERO=0 и отдельно добавляет
`apr_data & {16{mmu_active && D_select==0}}` через OR перед datapath.
MAP стал 1265 LUT. Hierarchical Synplify ORCALUT4 c→d: datapath 287→272,
engine с дочерними модулями 613→603, board bus 495→476. Эти числа включают
глобальное повторное mapping и вложенные модули; их нельзя складывать или
приписывать всю разность единственному RTL mux.

## Контракт микрокода и состояния

Selected d сохраняет только `VA[15:13]` в трёх FF на входе в context.
Все живые RF words остаются неизменны; исходный memory word возобновляется
через CP35 saved uPC. Адрес EBR — `{3'b000, page, pdr_select}`: только kernel
I table, используемая как unified. Наличие остальных записей в CP33 RAM
не означает поддержку processor modes или separate I/D в CPU.

Общий experimental backend сохраняет форму selector24 для варианта a.
В d A-поле APR_READ не используется: индекс страницы уже сохранён.
Production backend и формат v12 не меняются.

* `APR_READ, kind=PAR/PDR, target=label`: JUMP command0 с bit5=1,
  bit6 выбирает PDR. Bit4 по-прежнему отключает prefetch. Decode обязательно
  проверяет JUMP: у обычных stream READ bit5 уже занят и не означает APR.
* `d=APR`: ALU alias D=0, действующий только внутри private context.
  Вне context ZERO остаётся нулём; остальные D sources, MDR и IR сохранены.
* `MMU_RETURN`: прежний reserved bit1 CP35; отдельный saved uPC,
  обычный EA CALL/RETURN link не затрагивается.

Routine из девяти слов: save PSW, read PAR, save PAR, read PDR, save PDR
с word NZVC, JUMP, XOR PAR/PDR с NZVC, restore PSW, private return.
XOR служит проверке живого ALU/flags пути, не проверяет PDR permissions.
Все слова исполняются; с входным redirect получается 10 clocks.

Hold сохраняет uPC, ROM word, RF, Q и PSW. APR_READ во время hold может
повторно читать тот же адрес EBR; это не внешний запрос и не запись.
После перехода к ALU save EBR enable снят, его выход остаётся стабильным.
Reset сбрасывает context; page FF не требует reset, так как используется
только после нового entry. Ни один внешний memory request до возврата
из helper не разрешён. Исключение fault/IRQ работает по прежнему контракту.

## Проверки

* CPU miter: 69632 cases — все 65536 opcodes и 4096 seeded cases;
  390550 entry/return, 781100 PAR/PDR reads, все восемь страниц и все
  88 memory uPC; 4096 disabled cases, 34816 IRQ cases, wait/error injection.
  Сравниваются guest bus, R0–R7/T0–T4, PSW, Q, IR, MDR, CALL link и retirement.
  APR fixture меняется между cases, включая асимметричные bytes и bit15.
* 1024 cases дополнительно в four-state Icarus с hold; ещё 1024 без hold.
  Без hold получено ровно 19710 extra clocks на 1971 entry, внутренних
  ожиданий нет. При hold: `extra = 10 × entries + held_edges`.
* Reset проверен во всех девяти позициях helper, включая остановленные
  read/save/return. Все шесть внесённых дефектов обнаружены: PSW, page,
  PAR/PDR select, отключённое чтение EBR, запись при hold, guest byte flags.
  Assembler отвергает восемь вариантов неверных fields/range/overlap.
* Strict Verilator `--Wall` для default core и aligned-word/ROM-decode core.
* FIS: 23840 exact cases / 3072 injected faults на RAM и столько же на
  SPI FRAM; 645 cases / 83 faults на немодифицированной Lattice DP8KC model.
* Full-board cold RT-11FB + DIR: **412130048 clocks**, 3987390 retirements,
  5222610 reads / 424452 writes, 3397976 FRAM transactions, 300 RK commands,
  663 timer edges, 3270 UART wire bytes, 162 SD reads / 6 overlay writes.
  UART transcript совпадает с CP36; backing image не изменён.

Это не RT-11XM. Пользовательский `../lsi11/disks/rt11v5.3/system.dsk`
сохранён с исходным SHA256; CPU translation/MMR/high DMA ещё отсутствуют.
[Точные hashes и результаты](verification-cp37.json).

## Воспроизведение и следующий шаг

```
make test-mmu-apr-lookup
make test-mmu-apr-lookup-fis       # build/vendor DP8KC/GSR/PUR
make test-mmu-apr-lookup-board     # cold FB, read-only disk + RAM overlay
python3 tools/record_cp37.py
```

На Linux с Diamond после `make mmu-apr-lookup`:
`python3 tools/checkpoint_apr_lookup.py cp37e` в свежем build directory.
Старые отчёты не перезаписывать; rejected a/b/c воспроизводить из их
`source.tgz`. Там сохранены соответствующие версии generator и microcode.

Следующий gate — уменьшение общей площади CPU/board при сохранении этой
проверенной выдачи APR в ALU. Только затем подключать CPU CSR/arbitration,
MMR и translation. Целевой VA16/PAR16/PA22 с выбором 18/22 bits сохраняется;
RAM 128 КиБ не заменяет архитектурную поддержку старших физических адресов.
