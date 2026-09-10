# MMU / 128 КиБ FRAM — CP31–CP36

Решение пользователя от 2026-09-10: отложить FP11, оставить FIS и начать MMU.
Первоначальное ограничение «без MMU» относится к полученному baseline v1.
**В CP36 MMU ещё не подключён к CPU.** Ниже разделены рабочая сборка,
изолированный проверенный datapath и дальнейшая интеграция.

CP33 добавляет PAR/PDR store в одном EBR, W set/clear и canonical CSR decode.
Текущий isolated probe: **40 LUT / 65 FF (62 измерительных) / 1 EBR /
96.862 MHz**. Portable/vendor tests и CSR + translation differential прошли.
[Контракт, latency, resource evidence и границы CP33](mmu-apr.md).

CP34 исследует общую ALU: полное разделение увеличило datapath probe
471 → 487 LUT; разделение только relocation дало 466 LUT / 41.315 MHz.
В production это не перенесено. Анализ текущего microcode и CPU tests с
подменой регистров подтверждают, что T5–T7 свободны на границах памяти.
[Измерения, scratch contract и следующий эксперимент](mmu-sharing.md).

CP35 проверил microcode entry/return с сохранением PSW и CALL link.
CP36 сократил площадь opcode path: текущий production CP36f занимает
**1222 LUT / 326 FF / 6 EBR / 31.116 MHz**. С CP35 hook — **1243 LUT /
338 FF / 6 EBR / 31.107 MHz**, свободны 37 LUT и 14 slices. Полный MMU
ещё требует area gates; оба cold RT-11FB runs сохранили прежние counts.
[Измерения и contract CP36](area-decode.md).

## Целевой профиль: 18- и 22-битная адресация

Уточнение пользователя от 2026-09-10: MMU должен поддерживать **оба режима
J-11**, независимо от того, что на плате установлено только 128 КиБ FRAM.
18-битный CP31 — промежуточный resource probe, не окончательный профиль MMU.
Нельзя считать MMU завершённым после реализации только 18-битного режима.

По DEC §4.7.4.3 режим задают MMR0<0> и MMR3<4>:

| MMR0<0> | MMR3<4> | Отображение |
|---:|---:|---|
| 0 | любое | MMU выключен: 16-битный адрес, I/O page переносится в верх физического пространства |
| 1 | 0 | 18-битное отображение, пространство 256 КиБ |
| 1 | 1 | 22-битное отображение, пространство 4 МиБ |

Виртуальный адрес CPU остаётся 16-битным. PAR хранится **полностью, 16 бит**:
в режиме 22 bits `PA = ((PAR * 64) + VA[12:0]) mod 2^22`;
в режиме 18 bits результат ограничивается 18 битами. Затем верхняя
8-КиБ I/O page 18-битного пространства переносится в `17760000..17777777`
единого **22-битного физического интерфейса**. В 22-битном режиме диапазон
`00760000..00777777` сам по себе I/O page не является.
Canonical адрес MMR3 — `17772516` (VA `172516` при обычном отображении I/O).

При интеграции board decoder должен проверять все 22 бита до обращения к FRAM: RAM существует
только в `00000000..00377777`; в I/O page выполняется decode устройств;
остальное — NXM. Только после выбора RAM контроллер использует PA[16:0].
Усечение PA до 17 бит до decode запрещено: оно создало бы alias RAM и скрыло
обращение к неустановленной памяти. Поддержка 22 bits не означает наличие
4 МиБ RAM на этой плате и не обещает такой объём RT-11XM.

CP32 реализует этот контракт в `rtl/uj11_mmu_translate.v` с PAR16, PA22
и входами `enabled`/`map22`, соответствующими MMR0<0>/MMR3<4>.
Сами MMR и их CSR ещё не реализованы. Проверены переключение этих входов,
игнорирование `map22` при отключённом MMU, старшие PAR bits, wrap на 18/22 bits,
обе I/O-page mappings, граница 128 КиБ и NXM без alias. Старый
`uj11_mmu_translate18.v` сохранён как независимый miter для 18-bit случаев.

## Рабочая сборка

FP11 удалён из RTL, opcode dispatch, microassembler extensions, linker,
microcode и активных tests/build options. Эксперимент восстанавливается из
коммита `d59f19c` и snapshots CP30. Сохранены исторические отчёты.
FIS и весь integer core работают на прежнем store **954/1024×36 v12**:
70 свободных слов вместо 37. Generated EBR image побайтно совпадает с CP29.
Удаление FP words не уменьшает четыре физических microstore EBR.

RK CSR state перенесён из физических FRAM `0x10000..0x1001f` в слова
**`0x0f0..0x0ff` существующего firmware EBR**, 16×16 бит. Bootstrap занимает
426 байт; начало writable region — байт 480. Генератор запрещает overlap.
Вторая половина EBR содержит неизменный RK service (320 байт). Две 9-битные
EBR ports обслуживают low/high byte, byte lanes пишутся независимо.
Аппаратная проверка адреса запрещает запись вне этих 16 слов.

Firmware и RK CSR используют общий sequencer доступа к EBR; запись и
CS1/CS2 side effects происходят один раз за request. Warm/peripheral RESET
сбрасывает sequencing и CS1-visible state; содержимое RK words сохраняется,
как прежде в FRAM. При конфигурации FPGA эти EBR cells инициализируются нулём.
CSR reads во время RK service не меняют признак MOVB DMA. MMIO адреса прежние.
Новый EBR и основной RF для этого не понадобились.

**Все 128 КиБ FRAM освобождены от служебного RK state**, но текущий CPU
по-прежнему адресует только 16 бит. До подключения MMU это не означает,
что guest уже может использовать верхние 64 КиБ. SPI transport умеет оба
банка; стандартный board request пока передаёт bank=0.

## DEC contract первого datapath (CP31)

Первоисточник — [DEC DCJ11 User's Guide, EK-DCJ11-UG-PRE, Oct 1983](https://www.bitsavers.org/pdf/dec/pdp11/1173/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf),
§4.5.1–4.5.2, 4.7.1–4.7.4, 4.9. Для поиска использована
[OCR-копия того же руководства](https://dusted.dk/pages/computers/J-11/datasheet-DEC-DCJ11_Microprocessor_Users_Guide_OCR.pdf).
Локальный оригинал: `../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf`.
Существующие `../core/core.c`, `../tests/test_mmu_*.c` изучены как программный
эталон и источник тестовых сценариев; они не заменяют DEC specification.

* Виртуальный адрес остаётся 16-битным. Первый измерительный probe — 18 бит;
  17 бит адресуют установленную RAM, 18-й нужен для NXM и I/O decode.
* `PA = ((PAR[11:0] * 64) + VA[12:0]) mod 2^18`.
  Disabled MMU: `000000..157777` → RAM, `160000..177777` → physical I/O page.
* PDR PLF<14:8> задаёт границу в 64-байтных блоках, ED<3> — направление.
  Граница включена. J-11 ACF — **два** бита <2:1>: 00/10 nonresident,
  01 read-only, 11 read/write. PDR bit0 зарезервирован.
* Независимые ошибки nonresident/length/read-only соответствуют MMR0<15:13>.
  Неверный processor mode вызывает nonresident. Не моделируются optional
  дополнительные flags invalid-mode; length вычисляется по поданному PDR.

`rtl/uj11_mmu_translate18.v` получает уже выбранные PAR/PDR. Он возвращает
кандидат PA, error bits и RAM/I/O/NXM address classes; **при abort вызывающий
блок обязан подавить bus request**, а не использовать candidate PA.
Odd-word priority остаётся задачей интеграции, не этого combinational блока.
Cache, 22-bit mapping, tables, MMR state, W updates, traps и restart здесь
не реализованы. Новый RTL не входит в `tools/board_common.py` / production top.

Восьмеричная карта локального 18-битного PA **только для probe CP31**
(целевой интерфейс и canonical I/O mapping описаны выше):

| Диапазон PA | Назначение |
|---|---|
| `000000..377777` | Все 128 КиБ FRAM |
| `400000..757777` | NXM; запрещено усекать адрес и обращаться к FRAM |
| `760000..777777` | I/O page, с последующей проверкой наличия устройства |

## Реальные synthesis checkpoints

Diamond 3.14, `LCMXO2-1200HC-4SG32C`, constraint 29.56 MHz.
Каждый запуск имеет отдельный source snapshot, raw reports и hashes.

| Checkpoint / scope | LUT4 | FF | EBR | Slices | TRACE MHz | Microstore |
|---|---:|---:|---:|---:|---:|---:|
| CP31a — полный board, FP11 удалён | 1224 | 326 | 6 | 614 | 31.074 | 954 |
| CP31b — только translation/PDR probe | 55 | 70 | 0 | 35 | 110.742 | — |
| CP31c — полный board, RK CSR в EBR | 1252 | 326 | 6 | 628 | 30.917 | 954 |
| CP31d — тот же probe, общий length subtractor | 48 | 70 | 0 | 35 | 111.136 | — |

Все четыре MAP/PAR/TRACE gates прошли. Probe содержит **70 измерительных FF**,
сам translator комбинационный. CP31d заменяет два length comparators одним
8-битным вычитателем, его sign/nonzero отвечают за обе expansion directions.
На изолированном probe экономия **7 LUT**, те же проверки проходят.
Fmax probe нельзя выдавать за Fmax CPU с MMU; LUT двух независимых fits
нельзя механически складывать для обещания board fit.

CP31c оставляет **28 LUT / 12 slices / 1 EBR**. Даже размещение полного MMU
без register banking пока не доказано. Рост +28 LUT против CP31a исследован:
в hierarchical synthesis report bus вырос только на 2 ORCALUT4, core — на 25;
перестроились ALU, sequencer и decoder. Это эффект общей оптимизации/размещения,
а не точная независимая стоимость 32 байтов RK state. До интеграции tables
нужен отдельный gate площади; готовый большой MMU поверх почти полного FPGA
не добавляется.

## Проверки

* Integer differential: 12928 DCJ11 cases, PSW/RF/memory/traps, strict lint.
* FIS через synchronous decoder: 23840 cases, включая 3072 injected faults.
* Opcode decoder: все 65536 opcodes + enable holds, portable и vendor DP8KC;
  FP encodings снова идут в reserved trap entry.
* Firmware/RK EBR: 17920 reads/holds на каждом из portable/vendor вариантов;
  все byte masks, данные байта 0..255, ROM write protection, весь store.
* Board bus: 30 сценарных beats на portable и vendor EBR; RK CSR не меняют
  ни одного байта из 128 КиБ FRAM. Все 16777216 decode/mux combinations.
* FRAM transport: оба банка, byte/word/odd/held requests, сравнение всех
  128 КиБ model, SPI divisors 1 и 3. Это transport test, не CPU/MMU test.
* MMU18: **1769472 checks** на обоих вариантах length logic. Все PAR и
  block offsets, все PDR patterns на границах, disabled mode, NXM,
  отображение всех 16 физических 8-КиБ страниц через одно виртуальное окно
  без дыр и alias. Strict Verilator lint прошёл.
* CP31c cold RT-11FB + DIR: **354938300 clocks**, 3984366 retirements,
  3270 UART wire bytes, 162 SD reads / 6 writes. Backing image не изменён.

Команды: `make test-mmu18`, `make test-board-units`,
`python3 tools/check_sync_decode.py --suite fis`,
`python3 tools/run_board.py --tag UNIQUE`. Для synthesis — fresh directories,
`tools/checkpoint_board.py` для board и `tools/checkpoint.py` для probe.
Результаты и hashes: [verification-cp31.json](verification-cp31.json).
Плата не перепрошивалась; hardware baseline остаётся CP29a.

## CP32 — общий translation datapath и SPI FRAM

`rtl/uj11_mmu_translate.v` принимает выбранные PAR16/PDR16 и VA16.
Один 16-битный block adder выполняет relocation для обеих разрядностей;
его low 12 bits дают wrap на 18 bits. Общий 8-битный length subtractor
сохранён из CP31d. PDR rights/error contract прежний. Функциональные
регистры и таблицы сюда не добавлены; `map22` — вход, а не MMR3 CSR.

Canonical PA22 формируется до передачи в физический bus. RAM/I/O/NXM
классифицируются по block sum, не по усечённым FRAM pin bits. Например,
PAR=`007600`, VA offset=0 даёт I/O `17760000` при 18 bits и NXM `00760000`
при 22 bits. В 22-bit mapping для того же I/O адреса нужен PAR=`177600`.
Все адреса в этом примере восьмеричные.

| Checkpoint / isolated probe | LUT4 | FF | EBR | Slices | TRACE MHz |
|---|---:|---:|---:|---:|---:|
| [CP32a](../synth/reports/cp32a/result.json): nested PA mux | 72 | 80 | 0 | 42 | 74.102 |
| [CP32b](../synth/reports/cp32b/result.json): общий width/enable, ранняя классификация | 70 | 80 | 0 | 41 | 93.362 |

Оба MAP/PAR/TRACE gates PASS при constraint 29.56 MHz на HC1200.
Все **80 FF — стенд измерения** (52 stimulus + 28 observation).
CP32b сохраняет 14 CCU2D внутри translator, но сокращает PFUMX с 4 до 2;
LUT4 всего probe уменьшаются на 2. Это результат после synthesis/PAR,
не оценка стоимости добавления в CPU. Production inputs совпадают с CP31c,
его полный board fit не менялся: 1252 LUT / 326 FF / 6 EBR / 30.917 MHz.

Проверки текущего CP32b:

* **36144800 checks** в Verilator; дополнительный Icarus four-state проход
  **3114656 checks**. Все PAR16 × 128 block offsets × low offset 0/63 × оба
  включённых режима; все PDR16 patterns на границе и вокруг неё; все VA
  при выключенном MMU. Отдельно все 128 КиБ отображаются через одно окно
  ровно по одному разу в каждом режиме. 100000 последовательных изменений
  входов проверяют отсутствие старого PA/rights. Старый CP31 служит miter
  всех 18-bit случаев после нормализации его локального I/O адреса.
* **262144 differential cases** с существующим `../core/core.c`,
  `translate_va_ex`, `ENABLE_MMU=1`, модель DCJ11: 151902 успешных трансляции,
  110242 faults. Сравниваются success/fault и успешный PA; эталон C возвращает
  один fault, поэтому одновременные MMR0 error bits проверяет отдельный
  DEC-based тест. C локальный PA16/18 нормализуется только для I/O page.
  Выбранные PAR/PDR подаются прямо в RTL: это не проверка аппаратных
  tables, processor modes или separate I/D.
* **196634 beats / 24774246 clocks**: новый translator соединён с настоящим
  `boards/hc1200/uj11_board_fram.v` и моделью SPI FRAM. Записаны все 65536
  физических слов, каждое прочитано в обоих режимах; всё содержимое 128 КиБ
  сравнено с независимым byte array. Виртуальная I/O page отображается
  в RAM; проверяются byte/word, граница банков, последний байт, odd word,
  held request, NXM, PDR abort и I/O inhibit без побочных SPI transactions.
  Короткий four-state вариант — 413 beats / 50400 clocks. Здесь нет CPU,
  RK DMA или MMU CSR; testbench напрямую задаёт PAR/PDR.
* Strict `--Wall` lint translator/probe прошёл. В SPI Verilator test только
  width warnings импортированной модели отключены в локальной frozen copy;
  новые RTL/TB используют обычные fatal warnings. Исходная модель не изменена.

Команды: `make test-mmu-translate test-mmu-oracle test-mmu-fram`.
`python3 tools/record_cp32.py --run` запускает их с hashes до/после,
проверяет source snapshots и raw reports, архивирует журналы и сжатый C corpus.
Артефакты: [verification-cp32.json](verification-cp32.json),
[журналы и corpus](../tb/reports/cp32), [synthesis](synthesis.md).

MMU abort suppress проверен на границе translator→SPI, но аппаратные
MMR0/1/2/3, PAR/PDR store, PDR.W, vector250, restart/freeze и приоритет
odd/MMU faults ещё не реализованы. CPU MMU CPI, RT-11XM и полный MMU fit
не измерены. Физическая плата остаётся CP29a; прошивки в CP32 не было.

## Следующие gates и обязательный RT-11XM

1. PAR16/PDR storage и physical CSR decode измерены в CP33. CP34 показал лишь
   5 LUT экономии от hardware relocation sharing и отверг полное sharing.
   [CP35](mmu-entry.md) проверил entry/return с T5–T7, сохранением PSW/MDR/Q,
   занятого CALL link и однократностью memory request: 9 слов, +9 clocks/entry.
   CP36 сократил полный fixed-on board до 1243 LUT / 626 slices,
   свободно 37 LUT / 14 slices. Следующий gate — стоимость APR lookup
   и microcoded translation с дальнейшим сокращением общей логики.
   Измерить общую экономию LUT перед интеграцией translator и APR; одного свободного
   EBR достаточно для таблиц, но LUT budget полной MMU не подтверждён. Начать с kernel unified
   mapping, затем modes/SP switching и I/D отдельными gates. Не объявлять
   такой subset полным J-11 MMU. Не размещать эти таблицы в guest FRAM.
2. MMR0/1/2/3, выбор 18/22 через MMR3<4>, PDR.W, freeze и restart metadata; запрет внешнего запроса при
   abort, vector250, kernel virtual vector/stack cycles. Проверить одновременные
   memory/MMU faults и изменения регистров addressing modes.
3. Physical bus22, полный NXM/I/O decode до выделения PA[16:0] для FRAM.
   Разделить guest translation, private
   RK assist и physical DMA; extended DMA address по документации RK611/DMX.
   Прежний MOVB service на CPU с 16-битным BA сам по себе не даёт DMA >64 КиБ.
4. Differential tests и CPU read/write через все 128 КиБ, затем XM и SD/RK.

Пользовательский `disks/rt11v5.3/system.dsk` найден по пути
**`../lsi11/disks/rt11v5.3/system.dsk`**, 27540480 байт / 53790 секторов,
размер RK07, Volume ID `RT11A`. SHA-256:
`9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd`.
Read-only directory audit подтвердил `RT11XM.SYS`, `DMX.SYS`, `VMX.SYS`,
`DM.MAC` и `STARTX.BAK`; исходный образ не изменён.

**XM ещё не запускался на RTL с MMU**: такого integrated top пока нет.
Существующий FB testbench привязан к STARTF.COM и generic RT-11 banner;
простая подстановка этого образа не считается XM/MMU проверкой.
Новый XM gate обязан проверить именно XM banner, `SHOW MEMORY`, доступ
выше 64 КиБ, high-memory SD read/write/readback через DMX и отсутствие alias
нижнего банка. Команду выбора boot monitor и startup handshake определить
по документации RT-11 и содержимому образа, без изменения оригинала.
