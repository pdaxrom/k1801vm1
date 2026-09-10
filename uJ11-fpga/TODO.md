# uJ11 TODO

## Отложено: минимальный ODT

По решению пользователя от 2026-09-10 ODT отложен; текущий приоритет — MMU
и использование всех 128 КиБ FRAM. Для floating point остаётся FIS.

- [ ] Сделать монитор на PDP-11 assembly в отдельном firmware ROM; исходный
  бюджет — один EBR, 512×16 бит. Фактический размер подтвердить сборкой.
- [ ] Просмотр/изменение памяти, R0–R7 и PSW в восьмеричном виде, запуск по
  адресу, продолжение и повторный SD bootstrap через существующий KL11/UART.
- [ ] Обеспечить сохранение/восстановление состояния и вход при неисправном
  стеке программы; отдельно проверить взаимодействие с HALT, traps и RK assist.
- [ ] Проверить ROM, UART, сохранность FRAM/регистров и повторную загрузку RT-11;
  выполнить полный HC1200 synthesis/PAR/TRACE до программирования платы.

На физической плате остаётся CP29: 1239 LUT4, 326 FF и 6/7 EBR.
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
после MMU и нового измерения оставшихся ресурсов.

## Текущий приоритет: MMU / 128 КиБ FRAM

Новое решение пользователя от 2026-09-10 открывает следующий этап с MMU;
первоначальный запрет относился к уже полученному baseline без MMU.
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
- [ ] Следующий gate: сократить общую площадь до подключения APR CSR/write
  arbitration, MMR и translation. Размещение CP37d/e не считать fit полной MMU.
- [ ] MMR0/1/2/3, автоматический выбор APR/W updates, physical I/O page
  и NXM без alias верхней памяти в полном CPU/bus.
- [ ] Подключить translation к CPU; MMU abort 250, freeze/restart, odd faults.
- [ ] Разделить отображение гостя, RK firmware assist и физический DMA;
  реализовать старшие разряды RK DMA по документации контроллера.
- [ ] Проверить все 128 КиБ через CPU, SD/RK transfers выше 64 КиБ и MMU-off boot.
- [ ] Processor modes / SP switching / I-D spaces добавлять отдельными
  измеряемыми checkpoints; не заявлять полный J-11 MMU до их проверки.
- [ ] Differential tests, RT-11XM из `../lsi11/disks/rt11v5.3/system.dsk`: XM banner,
  SHOW MEMORY, RAM >64 КиБ, DMX read/write и full-board MAP/PAR/TRACE до прошивки.
