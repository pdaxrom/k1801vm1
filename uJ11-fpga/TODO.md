# uJ11 TODO

## Выбранное направление 2026-09-12: HALT ВМ2 и FP11 firmware

Пользователь выбрал механизм HALT ВМ2 с входом эмуляции по FP11 вместо FIS.
FIS остаётся в микрокоде; PSW/IPL J-11 сохраняются, служебный режим отдельный.
Полные ODT и FP11 устанавливаются в верхнюю FRAM программой под RT-11.
Это расширение uJ11 без MMU, не стандартное пространство
J-11. [Организация](docs/service-bank-proposal.md), [реализованный CP57e](docs/service-bank-cp57.md).
CP57e: 1260 LUT / 342 FF / 6 EBR / 31,982 MHz; на плате остаётся CP56a.
Выбран следующий профиль CP58a: 1246 LUT / 342 FF / 6 EBR / 30,743 MHz,
1002 слова; обычные STEP/SEL004 paths проверены. CP58b отклонён по площади.

- [x] Проверить документацию ВМ2/J-11 и текущие FRAM/HALT/FP11 пути.
- [x] Подробно разобрать [HALT ВМ2](docs/vm2-halt-reference.md): CPC/CPSW,
  SEL, фиксированные MFUS/MTUS, START/STEP и программная FIS.
- [x] Выбрать CPC/CPSW и программное сохранение R0–R6; полный микрокодный
  save не включать в выбранный механизм.
- [x] Определить минимальный HALT ABI CP57: CPSW16, один frozen context,
  отдельный режим, START и правила отложенных IRQ/trace.
- [x] CP58: STEP с пропуском одной проверки IRQ/T, SEL004 для обычных
  service faults и точный MFUS/MTUS fault delta; CPU/vendor/FRAM/RT-11 tests.
  [Контракт и ограничения](docs/service-bank-cp58.md).
- [x] CP58: полный HC1200 synthesis/MAP/PAR/TRACE и final EDIF audit.
  [Два измеренных варианта](docs/synthesis-cp58.json), выбран CP58a.
- [ ] До установки CP58 закрыть timing с допуском OSCH и внешние FRAM pins:
  Fmax 30,743 MHz ниже 29,56 × 1,055 = 31,1858 MHz; nominal gate недостаточен.
- [ ] Завершить HALT семантику ВМ2: copy H/P tracking, вложенные входы,
  SEL174/274 для ошибок незавершённого входа, внешний HALT для пультового STEP.
- [ ] Сократить footprint служебного профиля перед новыми аппаратными функциями:
  в CP58a свободны 34 LUT / 13 slices / 22 microinstructions.
- [x] Перенести служебные команды ВМ2 с фиксированными R0/R5 и HALT-only
  aliases; successful paths/USER checks и обычный SEL004 fault path проверены.
- [x] Ввести FP11 dispatch и отсутствие эмулятора; тестовый handler CP57,
  FIS сохраняет текущий результат и CPI (исходный FIS вход не меняется).
- [x] Измерить отдельный минимальный профиль service bank поверх CP56a:
  вход/возврат, сохранение контекста без гостевого стека, межбанковый доступ.
- [x] Включить в первый gate установочный доступ из обычного режима:
  RT-11 должна заполнять пустой верхний банк без уже работающего HALT firmware.
- [ ] Реализовать RT-11 loader `.SAV` и формат отдельных ODT/FP11 файлов:
  блочное чтение средствами ОС, перенос, readback/checksum, ready последним.
  Проверить ошибки файла/диска, прерывание загрузки, повторную установку,
  независимые ready-флаги и запрет автоматической активации старой FRAM после reset.
- [x] Проверить bank/CS isolation, I/O, прежний RK/cold FB, терминальные
  service faults/IRQ/trace контракта CP57; получить full HC1200 synthesis.
- [ ] Проверить полный RT-11 loader → установленный ODT/FP11 на реальной плате;
  получить внешний FRAM timing audit для новой разводки до её установки.
- [ ] После успешного gate реализовать ODT в верхней FRAM, загружаемый из
  RT-11; прежний вариант полного ODT в EBR заменён этим решением.
- [ ] Сделать два интерфейса общего ODT: UART и HDSP/20-key пульт.
  Адрес/данные, R0–R7/PSW, редактирование, START/CONTINUE/STEP, RGB-индикация.
  Оба используют общий контекст и загружаются из RT-11 в служебную FRAM.
- [x] Зафиксировать [раскладку пользователя](docs/panel-keyboard.md):
  0–F, MEM/PREV/NEXT/ENTER; отдельной ESC нет.
- [x] Сохранить схему/PCB пользователя; сопоставить S1–S20 с KC0–KC4,
  TMS/TCK/TDI/TDO и таблицей текущего сканера, указав границы подтверждения.
- [ ] Для пульта реализовать OCT/HEX и меню команд; сопоставить физические
  управляющие клавиши с кодами сканера до назначения действий.
- [ ] Адаптировать panel driver для HALT без RT-11 IRQ/PNWAIT; проверить
  debounce, переключение UART/панель и владение общими pins с HG.
- [ ] Отдельно определить запрос HALT с панели во время исполнения гостя;
  сам интерфейс остановленного монитора такого входа не обеспечивает.
- [ ] Затем оценить FP11 как PDP-11 firmware в верхнем банке; старый
  микрокодный FP11 автоматически не возвращать. Полный эмулятор загружает
  RT-11 loader; FIS сохраняется.

## Текущий приоритет: оптимизация MMU-less CPU и полного board

- [x] CP56: удвоить SPI FRAM до номинальных 29,56 MHz через ODDRXE;
  local/vendor/FRAM/cold FB PASS, R,R **1,70×**, cold FB+DIR **1,665×**.
- [x] CP56a: согласованная передача, полный HC1200 MAP/PAR/TRACE PASS:
  **1184 LUT / 339 FF / 6 EBR / 31,996 MHz**, от CP54b −1 LUT/−2 FF.
- [x] CP56: проверить routed FRAM input/output setup/hold и OSCH envelope;
  bounded TRACE PASS, но SCK pulse-width margin только **0,147 ns** при
  +5,5%, 43/57 duty и 2% period jitter. Negative +0,2 ns distortion пойман.
- [x] По явному запросу пользователя установить CP56a для проверки на HC1200:
  FLASH Erase/Program/Verify PASS, RT-11FB V05.03 и prompt получены;
  picocom восстановлен. [JED и журнал](docs/board-bringup-cp56a.md).
- [ ] Получить результаты пользовательской проверки CP56a: программы,
  RGB/HDSP, keyboard и HG (JTAG_EN в GPIO, scanner и HG по очереди).
- [ ] Измерить короткий SCK на плате и явно ограничить рабочие условия
  либо увеличить запас. Рассмотреть clock
  с контролируемой скважностью; простого сравнения 29,56 <34 MHz недостаточно.
  PCB flight/skew budgets пока предположены; CP56a установлен для проверки.
  [CP56 и точные timing constraints](docs/spi-cp56.md).

- [x] По запросу пользователя установить выбранный CP54b на HC1200:
  JED экспортирован, FLASH Erase/Program/Verify PASS, RT-11FB prompt получен,
  picocom восстановлен. [Журнал и точный JED](docs/board-bringup-cp54b.md).
  CP54b заменён на CP56a до получения отдельного отчёта по периферии;
  его JED сохранён для возврата.

- [x] CP55: shared RX на frozen CP54b; formal/negative/X/Z/reset/128 КиБ
  FRAM, portable/vendor board и cold FB+DIR PASS, counters/raw UART прежние.
- [x] CP55a: разрешённая передача и полный synthesis выполнены.
  1182 LUT / 333 FF / 6 EBR / 30,827 MHz; от CP54b −3 LUT/−8 FF,
  но −1,446 MHz Fmax. По решению пользователя основа — CP54b;
  CP55a сохранить как эксперимент, shared RX в следующие изменения не переносить.
  [CP55](docs/rx-cp55.md).

- [x] CP54: вынести DMA operand из адресного пути и проверить общий gate
  быстрых ACK. Два SAT proofs, три negative controls, 524288 X/Z-data cases,
  86 side-effect beats и 36 portable/vendor workloads PASS.
- [x] CP54: оба новых cold FB+DIR PASS, по 288686609 clocks;
  counters/raw UART совпали с CP53a, исходники и logs заархивированы.
- [x] CP54a/b: оба full synthesis PASS; выбран CP54b — 1185 LUT / 341 FF /
  6 EBR / 32,273 MHz, −10 LUT от CP53a. Все HDL/test/report hashes проверены.
  [CP54](docs/ack-cp54.md).

- [x] CP51: проверить MAP/TRACE и распределение ресурсов CP40h, связать
  их с текущим default RTL CP50. Свободны 121 LUT / 56 slices / 1 EBR.
- [x] CP51: измерить девять warm workloads на полном native board;
  R,R = 107 clocks/instruction, FRAM busy 96,26%. [Аудит](docs/resources-cp51.md).
- [x] CP52: отдельный sequential FRAM READ candidate; полная периферия,
  overlays/RK, cursor/write/reset tests, portable/vendor benchmarks и cold FB+DIR.
  R,R ускорен в 2,671×, полный cold сценарий — в 1,229×. [CP52](docs/fram-sequential-cp52.md).
- [x] CP52a/b: полный Diamond gate. Baseline 1159/326/6/31,470 MHz;
  sequential 1198/341/6/30,044 MHz. Оба routed/timing PASS.
- [ ] Уменьшить площадь sequential FRAM board до включения в default:
  у выбранного CP54b остаются 95 LUT, slack 2,843 ns; до <=1100 LUT ещё 85 LUT.
  Default CP52a пока занимает 1159 LUT; MMU и FP11 остаются отложенными.
- [x] CP53: EDIF уточнил 8 CCU2D для инкремента и 5 для сравнения;
  конечные netlists не содержат сетей с двумя сильными драйверами.
- [x] CP53: подготовить increment/compare/both, доказать equivalence,
  проверить FRAM/overlays и сохранить все benchmark counters CP52.
- [x] CP53a/b/c: full synthesis PASS; 1195/1204/1208 LUT. Выбран CP53a,
  −3 LUT от CP52b; новые cold FB/vendor tests сохранили counters/raw UART.
  [CP53](docs/cursor-cp53.md).
- [ ] После первого resource gate отдельно оценить малый instruction-stream
  buffer: CP52 ещё не выполняет speculative reads.
- [x] Измерить native-board shared RX: CP55a дал −3 LUT/−8 FF.
- [ ] Исследовать локальные read-data/decode преобразования полного native
  board с последующим MAP/PAR/TRACE; отдельные hierarchical counts не суммировать.
- [ ] Продолжить исследование address→ACK→uPC critical path: у CP54b
  31,012 ns, путь включает address[0]→request/write→DMA operand→ACK→seq.
  Сравнивать Fmax и memory clocks, сохранять odd-word faults и цель <=1100 LUT.
- [ ] Если возвращаться к эксперименту CP55, до аппаратной проверки задать
  external FRAM pin timing:
  RX bit 0 теперь в PFU result вместо отдельного PIO register; внутренний
  TRACE Fmax не доказывает запас на MISO.

## CP50: рабочая сборка без MMU

Решение пользователя от 2026-09-11: прекратить поиск ресурсов под MMU на
HC1200. Сохранить прототип под `UJ11_MMU`, default `MMU=0`.

- [x] Разделить MMU-less CP40h и эксперимент CP47c через compile-time guards.
- [x] Исключить MMU sources из default сборки; сохранить FIS и общий microcode.
- [x] Проверить обе ветви против архивов, FRAM/bus и CPU portable/vendor.
- [x] Новый full-board synthesis default профиля выполнен как CP52a,
  1159 LUT / 326 FF / 6 EBR / 31,470 MHz. MMU gate повторять не требуется.

[Профили и ограничения](docs/build-profiles-cp50.md).

## Историческое предложение: минимальный ODT в EBR

Предложение от 2026-09-10 ниже заменено решением от 2026-09-12:
полный ODT загружать из RT-11 в служебный банк FRAM. Активные задачи
находятся в начале файла; пункты ниже сохранены как история, не второй план.

- [ ] Сделать монитор на PDP-11 assembly в отдельном firmware ROM; исходный
  бюджет — один EBR, 512×16 бит. Фактический размер подтвердить сборкой.
- [ ] Просмотр/изменение памяти, R0–R7 и PSW в восьмеричном виде, запуск по
  адресу, продолжение и повторный SD bootstrap через существующий KL11/UART.
- [ ] Обеспечить сохранение/восстановление состояния и вход при неисправном
  стеке программы; отдельно проверить взаимодействие с HALT, traps и RK assist.
- [ ] Проверить ROM, UART, сохранность FRAM/регистров и повторную загрузку RT-11;
  выполнить полный HC1200 synthesis/PAR/TRACE до программирования платы.

На физической плате установлен CP56a: 1184 LUT4, 339 FF и 6/7 EBR.
Свободный EBR не резервируется под ODT.

## Отложено: FP11(A)

По последнему решению пользователя FP11 удалён из рабочей сборки, включая
RTL доступа к FP state, opcode dispatch, reset hook и 33 microinstructions.
Не оставлять его за параметром, занимающим control store. Рабочая версия
возвращается к 954/1024×36 v12; свободно 70 слов. FIS сохранён полностью.
Эксперимент восстанавливается из коммита `d59f19c` и source snapshots CP30.

- [x] Зафиксировать первичный профиль FP11-A и границу с J-11/FIS: [CP30](docs/fp11a.md).
- [x] Проверить стоимость постоянного FP state и доступа к нему до полной
  арифметики; сохранить основной RF16×16 и ALU16.
- [ ] Добавлять ISA и независимые проверки поэтапно с реальными HC1200 gates.

Исторический CP30: семь управляющих команд, FP-enabled full top 1265 LUT / 327 FF /
6 EBR, 31.284 MHz; всего 987/1024 microinstructions. Полный FP11 ещё
не реализован. Возвращаться к addressing modes, transfers и арифметике
только по отдельному решению и после нового измерения ресурсов.

## Отложено: MMU / 128 КиБ FRAM — сохранённая работа CP31–CP49

Решение от 2026-09-10 открыло эксперимент с MMU; решение CP50 от
2026-09-11 останавливает его на HC1200. Невыполненные пункты ниже —
отложенная работа, не текущий план. Измерения, RTL, microcode и тесты сохранены.
Текущий рабочий CPU пока имеет 16-битный физический интерфейс.
План и границы checkpoints: [MMU](docs/mmu.md).

Целевой MMU поддерживает **18 и 22 bits**, переключаемые MMR3<4>;
VA16, PAR16, единый PA22. Размер установленной FRAM не ограничивает
архитектурную разрядность MMU. CP31 с 18 bits — только промежуточный probe.

- [x] Удалить FP11 из активных RTL/microcode/build targets, сохранить FIS.
- [x] Подтвердить новый full-board baseline synthesis и регрессией RT-11FB.
- [x] Измерить отдельно 18-bit relocation и проверки PDR по документации DEC.
- [x] Перенести RK CSR state из первых 32 байтов верхнего банка FRAM в EBR (CP31c).
- [x] Расширить отдельный translator до 18/22 bits: вход выбора MMR3<4>,
  PAR16, PA22; проверить переключение, MMU-off, high PAR bits, wrap,
  I/O mapping и NXM. CP32b: 70 LUT / 80 probe FF / 0 EBR / 93.362 MHz.
- [x] CP32: сравнить translator с C MMU и проверить все 128 КиБ через
  модель SPI FRAM и board transport. Это ещё не CPU/MMR/SD integration.
- [x] CP33: изолированный PAR/PDR store в одном EBR, byte writes и W
  set/clear; decode всех 96 CSR без aliases. Portable/vendor/C oracle PASS.
- [x] CP34: измерить разделение ALU. Полное sharing дороже на 16 LUT;
  relocation-only экономит 5 LUT в datapath probe, в production не перенесён.
- [x] CP34: проверить время жизни T5–T7 на всех 88 memory words;
  static analysis, CPU/FIS/EIS poisoning и отрицательные контроли.
- [x] CP35: измерить microcode entry/return с T5–T7; PSW/MDR/Q и занятый
  EA CALL link сохранены. 9 слов, 12 FF, +9 clocks/memory word без hold.
  CPU miter, FIS/FRAM/vendor ROM и cold RT-11FB + DIR прошли.
- [ ] Сократить общую LUT cost перед подключением CP32/CP33 и MMR.
  CP36 освободил 30 LUT: production 1222 / 326 FF / 6 EBR / 31.116 MHz;
  с context hook 1243 / 338 FF / 6 EBR / 31.107 MHz, свободно 37 LUT / 14 slices.
  Полный MMU fit ещё не доказан. [Отчёт CP36](docs/area-decode.md).
- [x] CP36: уменьшить opcode index и исключить operand byte mux из входа
  opcode ROM; все opcode/hold, byte lanes, FIS/FRAM и оба cold FB runs прошли.
- [x] CP37: измерить read-only APR lookup и подачу PAR/PDR в context routine
  через общую ALU, сохранить guest context и блокировку внешнего запроса.
  CP37d/e: 1265 LUT / 341 FF / 7 EBR / 30.327 MHz; +10 clocks/memory word.
  CPU/FIS/FRAM/vendor/cold FB прошли. Это cost floor без CSR/translation:
  осталось 15 LUT / 5 slices / 0 EBR. [Отчёт](docs/mmu-apr-lookup.md).
- [x] CP38: сократить read mux полного board с исходными decoders. Production
  1188 LUT / 326 FF / 6 EBR / 30.943 MHz; с APR 1228 LUT / 341 FF / 7 EBR /
  32.470 MHz. Сэкономлено 34/37 LUT, formal и paired cold FB counts PASS.
  [Измерения](docs/area-board-read.md).
- [x] CP39: CPU APR CSR/write arbitration с общим EBR lookup; word/byte,
  paired W clear, odd vector4, reset persistence и physical RK DMA exclusion.
  Full board 1268 LUT / 344 FF / 7 EBR / 31.075 MHz, microstore 963 words.
  Portable/vendor CPU/bus/port, whole-opcode miter и cold FB counts PASS.
  [Отчёт](docs/mmu-apr-csr.md). Production остаётся CP38f, FPGA CP29a.
- [x] CP47 verification: SPI FRAM byte mux / shared RX на основе CP45k; formal,
  SPI/WREN/CS, banks/lanes, X/Z, reset, held request, CPU/vendor/bus и cold FB
  прошли с прежними clocks/UART. [Контракт rdata](docs/area-fram-cp47.md).
- [x] CP47 area gate: четыре полных HC1200 synthesis. Лучший shared-rx —
  1297 LUT / 351 FF / 7 EBR / 650 slices, −5 LUT / −8 FF относительно CP45k.
  Exact-source CPU/vendor/bus/cold FB и final EDIF audit прошли.
- [x] CP48: измерить explicit successors и one-hot FRAM на основе CP47c.
  1319/1313 LUT, +6 FF; отклонены. Formal/unit/CPU/vendor/bus и cold FB
  (successors) прошли. [Причина роста](docs/area-fram-state-cp48.md).
- [x] CP49 verification: проверить syn_encoding=original по руководству Synplify,
  подготовить explicit/original, split-low и bit equations. Formal/unit/
  CPU/vendor/bus и cold FB split-low прошли. [Отчёт](docs/area-fram-binary-cp49.md).
- [x] CP49 area gate: четыре полных HC1200 synthesis, actual state encoding
  проверен по SRR/EDIF. Original/split-low/equations: 1325/1335/1310 LUT,
  все хуже CP47c, не приняты. Final EDIF audit прошёл для A–D.
- [x] CP50: остановить дальнейшие area checkpoints для MMU на HC1200.
  CP47c сохранён под `UJ11_MMU`: 1297 LUT / 351 FF / 7 EBR / 650 slices.
  Все CP47–CP49 gates — MAP FAIL; Fmax и проверка RT-11XM отсутствуют.
- [x] CP40: перестроить operand/writeback mux и ALU result selection.
  Production 1159 LUT / 326 FF / 6 EBR / 31.470 MHz; APR 1258 LUT / 344 FF /
  7 EBR / 30.254 MHz, экономия 29/10 LUT. Formal/four-state/CPU/FIS и оба
  cold FB runs прошли, clocks прежние. [Отчёт](docs/area-datapath.md).
- [x] CP41: проверить masked/encoded mux микросеквенсора. Четыре варианта
  отклонены по площади/частоте; formal/simulation/lint PASS. Контроль CP40i
  повторил 1258 LUT / 344 FF / 7 EBR / 30.254 MHz. Экономии нет.
  [Отчёт](docs/area-sequencer-cp41.md). D-input engine/APR проверен в CP42.
- [x] CP42: выделить high byte D-input в экспериментальном APR engine.
  APR 1248 LUT / 344 FF / 7 EBR / 30.896 MHz, −10 LUT, без новых тактов.
  Production-вариант дал +1 LUT и отклонён; native CP40h сохранён.
  Formal/four-state/CPU/CSR/FIS/vendor и cold FB прошли, counts/CSV/UART
  совпали с CP40. [Отчёт](docs/area-d-input-cp42.md).
- [x] CP43: MMR3 CSR/reset в отдельной APR-сборке, 1258 LUT / 351 FF /
  7 EBR / 630 slices / 30.866 MHz, 963 words. Свободны 22 LUT / 10 slices.
  CPU/vendor, C differential, canonical decode и RK DMA isolation проверены.
  Только хранение bits; [CPU translation ещё не подключена](docs/mmr3-cp43.md).
- [x] CP44: проверить kernel unified CPU relocation 18/22, MMR0 software
  controls/MMR3<4>, PA22/NXM, оба банка SPI FRAM, byte/odd/mapped-stack/
  instruction-stream, C oracles и vendor EBR. Cold FB + DIR проходит,
  использует верхнюю FRAM, UART прежний. [Отчёт](docs/relocation-cp44.md).
  Это отдельный не поместившийся прототип, production CP40h сохранён.
- [x] CP45: bus I/O read factoring, exact prefix и narrow bootstrap ROM.
  1302 LUT / 359 FF / 7 EBR / 652 slices, −49 LUT / −24 slices от CP44.
  Одиннадцать synthesis gates, binary/four-state и CPU/FRAM/vendor/bus/
  cold FB tests; все clocks и UART прежние. [Отчёт](docs/area-bus-cp45.md).
  MAP FAIL, в production не принят; FPGA CP29a не программировалась.
- [x] CP46: PA17 + RAM/I/O qualifiers, phase equations и APR port mux.
  Все четыре варианта хуже CP45: 1315–1317 LUT, MAP FAIL, не приняты.
  Formal/mutations, C oracle, APR portable/vendor, upper FRAM и cold FB PASS.
  Конечные EDIF и negative controls проверены. [Отчёт](docs/area-control-cp46.md).
- [ ] Получить fit с резервом для полного MMU, затем принять CPU relocation.
- [ ] MMR0 hardware fault/page metadata, MMR1/2, оставшиеся MMR3 controls,
  PDR protection/automatic W, MMU abort250 и freeze/restart в полном CPU/bus.
- [ ] Разделить отображение гостя, RK firmware assist и физический DMA;
  реализовать старшие разряды RK DMA по документации контроллера.
- [x] CP44: все слова верхних 64 КиБ через CPU в 18/22 modes, snapshot
  нижнего банка неизменён; mapped FB startup и cold boot + DIR проверены.
- [ ] Проверить активный RK transfer при MMU-on и SD/RK transfers выше 64 КиБ;
  CP44 cold FB не выполнял private ROM/DMA обращений при включённой MMU.
- [ ] Processor modes / SP switching / I-D spaces добавлять отдельными
  измеряемыми checkpoints; не заявлять полный J-11 MMU до их проверки.
- [ ] Differential tests, RT-11XM из `../lsi11/disks/rt11v5.3/system.dsk`: XM banner,
  SHOW MEMORY, RAM >64 КиБ, DMX read/write и full-board MAP/PAR/TRACE до прошивки.
