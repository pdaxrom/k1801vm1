# uJ11 TODO

## Актуальный остаток на 2026-09-22

Сверено по коммитам и сохранённым результатам до **CP82** включительно.
На плате подтверждены FPGA CP67b, ODT CP77, программный FPP J‑11 CP80,
FIS/FPU BASIC и отключение/возврат FPP через cold boot.
[Версии, файлы и размещение во FRAM](docs/software-current.md).

**Доработки программной части:**

- [ ] `ONn` в UJMOD: включать сохранённый VALID-модуль без повторного чтения
  BIN с SD; checksum и инициализация при cold boot обязательны. Сейчас
  проверенный способ возврата — загрузка по имени файла, затем RESET.
- [ ] Relocatable-модули: relocation records, исправление адресов при загрузке
  и checksum размещённого образа. Сейчас все модули абсолютные.
- [ ] Учёт и автоматическое размещение BSS/стеков. Сейчас loader проверяет
  immutable images; полные диапазоны рабочих областей согласуются вручную.

**Незавершённая квалификация:**

- [ ] Внешняя диагностика именно J‑11 FPP или сравнение с физическим DCJ11.
  Внутренние матрицы, BASIC и аппаратные STEP уже пройдены; результаты
  FP11-A DFFPA/B/C из CP77/78 не заменяют проверку целевого профиля J‑11.
- [ ] Полный физический проход 20 клавиш и команд ODT, включая P/Z/U/T
  и FP-регистры. Базовая работа пульта подтверждена; полный проход пока
  не записан. UART-проверки не считаются проверкой каждой кнопки.
- [ ] Останов ODT **внутри** HG-транзакции, таймаут и восстановление.
  Успешные передачи до и после отладки между COPY уже проверены.
- [ ] Непрерывный UART burst и поведение при переполнении приёмника.
- [ ] Аппаратные проверки обрыва питания при записи FRAM и recovery при
  зависшем initializer. Пройденные simulation и UART ESC recovery этого
  не доказывают; для разрушительной пробы нужна резервная копия модулей.
- [ ] Измерение SCK/PCB задержек и подтверждение рабочих условий FRAM.
  Routed timing с заданными budgets пройден; измерений осциллографом нет.

**Последующие улучшения:** программное ускорение FPP по SPI benchmarks;
RAW HALT/I/O в ODT, редактирование FP-регистров, десятичное FP-представление,
автоповтор клавиш/курсор и дополнительные RGB-состояния.
Аппаратные исследования — сокращение площади полного компьютера,
critical path памяти и отдельная оценка instruction-stream prefetch;
старые эксперименты CP52–56 не означают, что эти улучшения уже приняты.

**Отложено:** MMU 18/22 и RT-11XM, processor register banks/mode SP/I-D,
обычный integer PSW `177776`, общий NXM→HALT assist и оставшиеся особенности
HALT ВМ2 (H/P, вложенные входы, SEL174/274). PSW-операнды программного FPP
уже реализованы в CP80. Аппаратный/микрокодный FP11 не является текущим планом.
Аппаратный запас CP67b — **36 LUT, 12 slices, 0 EBR, 19 uwords**;
новое RTL требует отдельного resource/timing gate.

Дефекты эталонного общего `core/`, отмеченные CP70, относятся к отдельному
проекту; в uJ11 firmware их нет. В этой работе `core/` не меняется.

## История checkpoint и подробности

Ниже сохранены результаты и планы соответствующих этапов. Старые цифры и
формулировки «пока» относятся к тому этапу. Дубли задач, завершённых позднее,
отмечены ссылкой на завершивший CP; экспериментальные CP не требуют отдельной
прошивки задним числом. Для следующей работы использовать список выше.

## CP82 — гибкое модульное конфигурирование

- [x] На плате отключить AUTO FPP, записать свежий cold boot и проверить исходный B81FIS без адаптера: 29 сравнений и семь команд ошибок/восстановления — PASS.
- [x] Проверить B81FIJ без FPP; вернуть неизменённый FP11 с SD и после второго cold boot повторить FIS/FPU single/double. Таблица модулей восстановлена, ODT/SDBOOT не изменились.
- [x] Полный RTL OFF/cold/restore, CONFIG RT-11, исходные FIS-ошибки и ODT без FPP: 206 checks / 582937544 clocks — PASS. Сохранить исходники и журналы CP82.
- [ ] Добавить `ONn` в UJMOD для повторного включения сохранённого VALID-модуля без копирования BIN; сохранить проверку checksum и активацию на cold boot. Сейчас возврат через имя файла проверен, ON не реализован.

## CP81 — BASIC как прикладная проверка FIS/FPP

- [x] Найти BASIC в локальной коллекции: `lsi11/disks/rt11v5.3/basic.dsk`, DEC BASIC-11 V2.1, библиотеки FIS/FPU.
- [x] Собрать оригинальными SUCNFG/LINK варианты FIS single, FPU single и FPU double; исходные образы не менять.
- [x] Эталон SIMH: по 29 числовых проверок в каждой сборке и отдельная проверка double — PASS.
- [x] Полный CP67b RTL: 22 проверки стенда, 1322444008 clocks; три набора по 29 сравнений и double — PASS. 654 FIS, 9899 входов HALT FPP; обращений к USER 177776 нет.
- [x] Установить три исходных BASIC через HG, сверить пять readback. На плате FPU single/double, числовые тесты и error recovery — PASS; у исходного FIS обнаружена несовместимость стека .SFPA при активном FPP.
- [x] Локализовать FIS сбой: CPU сохраняет правильный PSW=013; RT-11 добавляет FEC/FEA. Сделать 26-байтовый FISABI по документации DEC, собрать отдельный B81FIJ. SIMH и полный RTL (17 checks / 384497709 clocks) — PASS.
- [x] Установить отдельный B81FIJ через HG: 27136 байт SD readback совпали; на плате 29 сравнений и семь команд ошибок/восстановления — PASS 2026-09-20. STATUS трёх FRAM-модулей до/после 140407. Исходный B81FIS сохранён.
- [x] По решению пользователя отложить обычный CPU PSW по `177776` и общий NXM→HALT software assist. CP78 сохранить как эксперимент; текущую плату не усложнять. PSW-операнды внутри программного FPP CP80 остаются.

## CP80 — PSW-операнды программного FPP

- [x] Перехватить word operands/EA pointers/extension по 177776 в сохранённый USER PSW, без нового RTL.
- [x] Сохранить T, обнулить reserved PS<10:9>, разделить явную запись CPU NZVC и conversion FPS.
- [x] Проверить EA, PC/wrap, частичные записи, bus/address faults, IRQ/trace/ODT и сброс признака между FP-командами.
- [x] Квалификация CP80: 712/25823 PSW в трёх конфигурациях; прежние sync 444420/30934144 и logic 215760/15037488; events portable/vendor 111/4174; RT‑11 99 checks. Пакет и исходные результаты заморожены.
- [x] Установить CP80 через RT‑11/UJMOD: SD readback/cold init, 22 PSW/FPS шага, 43 STEP и native FPTST — PASS 2026-09-19. Память и CPU-контекст пробы восстановлены. [Журнал](docs/board-fpp-cp80.md).

## CP79 — целевой программный FPP J‑11

- [x] Заменить FP11-A-specific unary/load FIUV на семантику DCJ11.
- [x] LDC exponent-zero: флаги по нулевому результату.
- [x] ADD/SUB по точности DCJ11 §7.6, exact-integer reference.
- [x] Полный sync 444420/30934144, logic 215760/15037488; directed portable/vendor 111/4174 каждый; RT‑11/ODT 99 checks. Замороженный CP79 и пакет подготовлены.
- [x] Установить на физическую плату через HG/UJMOD: readback всех файлов, cold init, 43 STEP PC/PSW, FP dump и FPTST — PASS 2026-09-15. [Журнал](docs/board-fpp-cp79.md).
- [x] Реализовать и проверить PSW-операнды программного FPP — CP80.
- [ ] Подобрать внешнюю диагностику J‑11 FPP; A-specific результаты CP78 не переносить.

[Профиль DCJ11 и источники](docs/fpp-j11-cp79.md).

## CP78 — memory-mapped PSW

- [x] Реализовать слово/байты PSW, T protection, NZVC precedence, PS<10:9>=0 по DEC.
- [x] Проверить USER/HALT, физический bypass, odd/error, IRQ/trace; portable/vendor.
- [x] Пройти DFFPA/DFFPB/DFFPC на реальном RTL CPU без test-only adapter.
- [x] Полный RT-11/UJMOD/ODT/FP11 регресс CP78: 99 checks, 43 FP шага, cold init/OFF — PASS; исходники и логи сохранены.
- [x] Выполнить synthesis CP78a/b/c и сохранить отчёты: все три не прошли MAP (649/643/648 slices).
- [ ] Отложено по решению CP81: найти способ разместить CPU-local PSW; лучший CP78b превышает лимит на 3 slices. Fmax ещё не получен.
- [x] FP11 firmware: word operands 177776 читают/пишут сохранённый USER CPSW, с защитой T и fault/flags precedence — CP80. Нечётное слово 177777 вызывает address fault. Байтовых операндов в FPP нет; CPU byte PSW interface остаётся частью аппаратного CP78.
- [ ] Отложено вместе с CP78: только после аппаратного gate собрать JED, установить и проверить UART/пульт/FRAM-модули на плате.

[Реализация, источники DEC и команды](docs/psw-cp78.md).

## CP77 — ODT FP и внешние диагностики

- [x] FP disassembly, CPU/AC operand forms, live FPS у остановленного PC.
- [x] UART F и один FP-регистр за раз на пульте, прежний scroll.
- [x] Развести расширенный ODT и FP11 на 060000, descriptor состояния.
- [x] 4096 opcode/1008 FPS-context проверок, прежний ODT/IRQ/panel регресс.
- [x] Извлечь DEC FFPAA1/FFPBA0/FFPCB0, проверить chain/loader checksums.
- [x] По одному проходу трёх DEC FP11-A частей в RTL с test-only PSW read.
- [x] Реализовать memory-mapped PSW и native DEC diagnostics: функциональная часть CP78 выполнена. Hardware gate остаётся в списке CP78 выше.
- [x] ODT CP77 и программный FPP J11 установлены как пакет CP79; аппаратные проверки прошли 2026-09-15. Прежний FP11-A профиль заменён по запросу пользователя.

[Команды, раскладка FRAM и границы PASS](docs/odt-fp-cp77.md).

## CP76 — преобразования FP11-A

- [x] STEXP/LDEXP; STCfi/LDCif I/L/F/D; STCff/LDCff F/D, все addressing modes.
- [x] NZVC процессора и FPS, min I/L, LONG register/immediate, FT, exceptions.
- [x] LDF/LDC flags при memory FIUV по DEC; изменения эталона помечены явно.
- [x] 251328 независимых числовых сравнений; 444444 RTL cases, logic/vendor.
- [x] 111 directed IRQ/ODT/fault cases; 403 SPI FRAM measurements.
- [x] Native RT-11/UJMOD/43 STEP, самопроверка, cold init/OFF и checksum.
- [x] Release и замороженный архив CP76, без изменения аппаратуры CP67b.
- [x] Внешние FP11-A DFFPA/B/C — CP77/78; прикладные BASIC — CP81/82.
  Внешняя квалификация целевого J‑11 остаётся в актуальном списке выше.
- [x] FP disassembly и AC/FPS/FEC/FEA в ODT для UART и пульта — CP77.
- [x] Установить программный FPP на плату и проверить через UART — CP79/80.
- [ ] После diagnostics оптимизировать STCfi shifts и dispatch: есть SPI baseline.

[Семантика, точные границы проверок и измерения](docs/fp11-conversions-cp76.md).

## CP75 — программные MODF/MODD

- [x] Разделение произведения по DEC: целая часть с усечением, дробная
  с округлением/усечением, 59-битный D product, чётные/нечётные AC.
- [x] Проверки двух AC, aliases, F low halves, ошибок и flags;
  направленные округления дроби до +1/−1.
- [x] 200000 сравнений Python/C128 и ещё 200000 с неизменённым MOD helper.
- [x] Logic 81544 / 5707456; vendor 276 / 19712;
  directed sync/vendor 49 / 1742 каждый; SPI FRAM 4 / 848.
- [x] Native RT-11/UJMOD/29 STEP, собственная FPTST и cold init/OFF:
  80 checks / 689803088 clocks / 6825 UART bytes.
- [x] 3420 байт HALT FRAM (+400); 53 hardware inputs CP67b прежние.
- [x] Полный sync: 225084 / 15632368; все 194396 fixtures CP74 сохранены.
  Release и замороженный архив CP75.
- [x] Преобразования FP11-A — CP76; внешние diagnostics остаются отдельно.
- [x] FP disassembly и AC/FPS/FEC/FEA dump в ODT — CP77.
- [x] Установить программный FPP на плату и проверить через UART — CP79/80.
  Установлен позднейший профиль J‑11; отдельный пакет CP75 не прошивался.

[Документация CP75](docs/fp11-mod-cp75.md).

## CP74 — программные MUL/DIV F/D

- [x] Умножение 24/56 и деление 31/63 итераций, все addressing modes,
  F/D округление и усечение, FIV/FIU/FIUV/FID, F low-half preservation.
- [x] DIV zero: сохранить AC/NZVC, коммитить EA update, FEC=4;
  IRQ/ODT видят завершённую инструкцию и целый результат.
- [x] По 200000 сравнений с uint128/Fraction/serial моделями;
  сохранить все fixtures CP73, отметить immediate-abort дефект эталона.
- [x] Измерения SPI FRAM: 241 операция; модуль 3020 байт, FPGA +0.
- [x] Sync: 194396 / 13529088, logic: 64248 / 4519200,
  vendor: 304 / 21764; RT-11/UJMOD/22 ODT STEP: 66 checks — PASS.
  Directed sync/vendor: 43 / 1527 каждый; архив и native release CP74.
- [x] MODF/MODD — CP75; преобразования и полные diagnostics ещё впереди.
- [ ] Сократить программные MUL/DIV и общий dispatch после correctness
  baseline: исследовать частичные произведения через имеющийся integer
  EIS MUL и выход DIV при нулевом остатке, без дополнительного RTL.
- [x] FP disassembly/dump — CP77; установка и проверка FPP J‑11 — CP79/80.

[Документация CP74](docs/fp11-muldiv-cp74.md).

## CP73 — программные ADD/SUB F/D

- [x] Семь guard bits FP11-A, нормализация, округление/усечение,
  overflow/underflow, все addressing modes и F low-half preservation.
- [x] Исправить ABS/NEG: запись нуля и flags перед memory FIUV exception.
- [x] Независимые uint64/Python эталоны: 200000 сравнений, точные
  границы ошибки; общий emulator оставлен без изменений.
- [x] Logic: 46040 cases / 3254816 checks; directed sync/vendor:
  39 cases / 1389 checks каждый; SPI FRAM: 4 / 732 — PASS.
- [x] Полный sync: 149404 / 10426000, vendor: 472 / 33088;
  RT-11/UJMOD/18 ODT STEP: 56 checks, cold init и OFF — PASS.
- [ ] Оптимизировать программные shifts/pack и dispatch: ADD F/D в
  измеренном AC случае 65229/67862 clocks; старые memory/unary paths
  получили +384–576 clocks. Сохранить correctness baseline CP73.
- [x] MUL/DIV F/D — CP74; MOD и преобразования остаются отдельным этапом.
- [x] FP disassembly/dump — CP77; проверка текущего FPP на плате — CP79/80.

[Документация CP73](docs/fp11-arithmetic-cp73.md).

## CP72 — ускорение FP11 AC transfers

- [x] Убрать промежуточный FBUF при LDF/LDD AC→AC, фиксированный перенос
  2/4 слов, сохранение младшей половины в F mode и корректный self-copy.
- [x] Проверять AC6/7 до доступа; пропускать autoupdate bookkeeping в mode 0.
- [x] Повторить полный sync/logic набор с побайтно прежними vectors/expectations.
- [x] IRQ/ODT между записями AC: 35 directed cases / 1254 checks в sync/vendor.
- [x] SPI FRAM: ускорение LDF/LDD 17–19%, STF/STD 6–7%; все 165 прочих
  измерений прежние. Цена — 30 байт FRAM, без дополнительных ресурсов FPGA.
- [x] DP8KC: 467 cases / 32176 checks; RT-11/UJMOD/ODT: 43 checks,
  включая 11 STEP, самопроверку, cold reset и OFF — PASS в симуляции.
- [x] ADD/SUB F/D с FP11-A rounding, overflow/underflow и cancellation tests — CP73.
- [ ] Дальнейшая оптимизация dispatch: LDF/LDD AC всё ещё +548/+425 clocks
  к CP70, STF +115. Memory LDF/LDD +2341…2400, STF/STD +1652 остаются.
- [x] FP disassembly/dump — CP77; установка текущего FPP на плату — CP79/80.

[Контракт CP72, сравнение и результаты проверок](docs/fp11-paths-cp72.md).

## CP71 — FP11-A unary и compare

- [x] CLR/TST/ABS/NEG/CMP F/D, все addressing modes, AC0–AC5 и AC6/7 illegal.
- [x] F-mode low-half preservation, dirty/negative zero, CMP exact-zero AC
  special case и различия в последнем слове D.
- [x] Документированное FP11-A TST flags-before-UV; отдельно от J-11 oracle.
- [x] 97176 differential cases и 1928 маркированных manual expectations;
  faults, partial writes, IRQ/ODT, cold init и сохранение ODT/SDBOOT.
- [x] RT-11/UJMOD/ODT: одиннадцать FP STEP, самостоятельная проверка FPTST,
  возврат, DIR, cold reset и OFF — 43 checks PASS в симуляции.
- [x] ADD/SUB F/D с FP11-A rounding, overflow/underflow и cancellation tests — CP73.
- [x] MUL/DIV F/D — CP74; преобразования и полная FP11 ISA ещё впереди.
- [ ] Устранить измеренную регрессию CP71: LDF/LDD AC5 +4255/+4777 clocks,
  memory +2341…2400; STF/STD AC5 +1225, memory +1652 относительно CP70.
  Проверить короткий путь AC transfer, общий dispatch и flags helper.
- [x] FP disassembly/dump с версионированным ABI — CP77.
- [x] Установка текущего FPP и UART-проверка — CP79/80; FPGA CP67b прежняя.

[Контракт FP11-A, отдельные ожидания oracle и measurements](docs/fp11-unary-cp71.md).

## CP70 — FP11 F/D transfers

- [x] LDF/LDD, STF/STD: все addressing modes, AC0–AC5, AC6/7 illegal,
  F-mode low-half preservation, immediate padding, dirty/negative zero.
- [x] FIUV/FID, partial read/write faults и autoupdates, IRQ и ODT внутри LDD.
- [x] 41676 differential cases плюс 136 отдельно отмеченных ожиданий
  для двух дефектов oracle; общий `core/` не изменялся.
- [x] Измерить переносы на SPI FRAM и повторить cold init/ODT/FP coexistence.
- [x] RT-11/UJMOD/ODT: десять FP STEP, проверка FRES/DRES/BACK самой
  программой FPTST, возврат в RT-11, cold reset и OFF — 48 checks PASS.
- [x] CLRF/TSTF/ABSF/NEGF и CMPF в F/D — CP71; арифметика и преобразования впереди.
- [ ] Отдельно исправить в общем emulator отмену trap для invalid AC6/7 и
  отсутствующий abort check в immediate ReadFP. Reproducer и объяснение
  сохранены в [CP70](docs/fp11-transfers-cp70.md); в uJ11 firmware дефектов нет.
- [x] FP disassembly/dump с версионированным module ABI — CP77.
- [ ] Оптимизация короткого register path управляющих команд.
- [x] Арифметика и преобразования — CP73–76, J‑11 — CP79; установка — CP79/80.

## CP69 — FP11 memory status instructions

- [x] LDFPS/STFPS/STST, все восемь режимов, R6/R7, immediate/absolute/PC-relative.
- [x] Отложенные autoupdates, ошибки extension/pointer/data, partial STST write,
  нечётные адреса и USER trap frame; 5012 differential cases.
- [x] Сохранить IRQ/trace/ODT regression; SPI FRAM cold init и checksum rejection.
- [x] RT-11/UJMOD/ODT: пошаговое исполнение memory commands, проверка FPTST,
  возврат в RT-11, cold reset и OFF модуля — 33 checks PASS в симуляции.
- [x] Архивировать 950-byte module, native assembly и SPI measurements.
- [x] LDF/STF и F/D представление AC — CP70. Арифметика и преобразования впереди.
- [ ] Вернуть короткий register path: CP69 LDFPS/STFPS Rn медленнее CP68
  на 1698/1960 clocks из-за общей EA подготовки.
- [x] Дифференциальные проверки FP11-A и benchmarks — CP73–76;
  профиль J‑11 и его регресс — CP79/80.
- [x] Установить текущий FPP на физическую плату и проверить UART — CP79/80.

[Контракт, документация DEC и результаты CP69](docs/fp11-memory-cp69.md).

## CP68 — программный FP11 в HALT FRAM

- [x] Перечитать DEC FP11-A: FPS, AC0–AC5, управляющие команды и исключения.
- [x] FP11.BIN, абсолютный модуль ABI3, cold init через общую таблицу CP67.
- [x] CFCC, SETF/SETD, SETI/SETL, LDFPS Rn, STFPS Rn: 21 кодировка;
  456 байт кода + 212 байт BSS/stack, без изменения RTL/микрокода.
- [x] 90 112 differential cases; exceptions/FID, faults, IRQ/trace/debug;
  ODT/FP/SDBOOT cold init в обоих порядках и повторный reset на SPI FRAM.
- [x] Реальные RT-11/UJMOD в симуляции: установка, cold init, UART ODT STEP
  через FP-команды, возврат в RT-11, повторный reset и OFF отдельного модуля.
- [x] Измерить все управляющие команды на SPI FRAM: 8145–9677 clocks,
  вход до handler 316, START fetch→USER return 177 clocks.
- [x] Memory addressing modes LDFPS/STFPS и STST, включая ошибки и SP/PC — CP69.
- [x] LDF/STF и представление F/D в AC — CP70; арифметика и преобразования впереди.
- [x] Дифференциальные проверки FP11-A и benchmarks — CP73–76;
  профиль J‑11 и его регресс — CP79/80.
- [x] Установить текущий FPP на физическую плату и проверить UART — CP79/80.

Это только управляющая часть: арифметики нет. FIS остаётся в микрокоде,
старый аппаратный FP11 не возвращается. [CP68: контракт и результаты](docs/fp11-firmware-cp68.md).

## CP67 — retained FRAM modules

- [x] Таблица из BASE/LENGTH/CHECKSUM/STATUS, без типов модулей; cold init по BASE.
- [x] ROM recovery через UART ESC до первого вызова FRAM; сохраняется встроенный bootstrap.
- [x] ODT с самоинициализацией и отдельный заменяемый SDBOOT; абсолютные образы.
- [x] Первый synthesis CP67a: 1244 LUT / 381 FF / 7 EBR / 32,032 MHz, timing PASS.
- [x] Directed portable: 30 сценариев / 249 checks; прежние 112 ODT breakpoint checks.
- [x] Vendor-ROM: 30 cases / 249 checks; полный RT-11/UJMOD/reset/recovery:
  55 checks / 1 023 462 941 такт / 3983 байта UART — PASS. Исходники, native
  assembly и сырые результаты архивированы; добавлен проверяющий скрипт.
- [x] Окончательный CP67b synthesis: 1244 LUT / 381 FF / 7 EBR / 32,032 MHz;
  экспортирован JED, исходники и timing проверены по SHA256.
- [x] CP67b записана во FLASH с Verify; RT-11FB V05.03 и DIR проверены через UART.
- [x] Установить ODT/SDBOOT/UJMOD на SD с полным readback, зарегистрировать во FRAM;
  длинный RESET → оба `140407`, короткий RESET → ODT R/D/C и возврат в RT-11 — PASS.
- [x] UART ESC recovery на физической плате: новый RT-11 boot, обход ODT
  при коротком RESET, сохранность всех восьми записей. Обычный RESET без ESC
  вернул ODT; R/D/C и RT-11 DIR прошли без переустановки модулей.
  [Аппаратные журналы и границы проверки](docs/board-recovery-cp67.md).
- [ ] Relocatable modules: формат relocation records, исправление ссылок при
  установке и checksum уже размещённого образа; четыре поля FRAM-таблицы сохранить.
- [ ] Автоматическое размещение BSS/рабочих областей, если понадобится несколько
  динамически размещаемых модулей. Сейчас диапазоны заданы при сборке.

[ABI, восстановление и ограничения](docs/retained-modules-cp67.md).

## План ODT от 2026-09-12

- [x] CP64 hardware: прошить точную CP63b, проверить FLASH Verify, RT-11FB
  boot и DIR. [Журнал установки](docs/board-bringup-cp64.md).
- [x] Через HG установить UJLOAD/ODT/UJON на SD, проверить SHA256 обратных
  копий, загрузить ODT в HALT FRAM и включить debug через UJON.

[Подробный план отладчика](docs/odt-debugger-plan.md).

- [x] CP63: RTL кнопки RESET — короткое отпускание запрашивает ODT, удержание
  около 2 s вызывает reset; независимые POR/debounce/timebase, directed tests.
- [x] CP63: безопасный внешний HALT и настоящий STEP с повторным входом; сохранить
  контекст, T/IRQ/WAIT и отделить останов от сигнатурного вызова загрузчика.
- [x] Первый HC1200 synthesis gate: CP63b **1230 LUT / 381 FF / 6 EBR /
  32,246 MHz**, 1005 uwords, 31,824 MHz + FRAM PASS.
- [x] CP64: загружаемый UART ODT, регистры/PSW, память, STEP/CONTINUE,
  integer/EIS/FIS disassembly, 8410 байт с данными/стеком из 12 КиБ FRAM.
- [x] CP64: HDSP/клавиатура, OCT/HEX, меню, редактирование и прокрутка;
  при активном HG TDO панель не захватывается.
- [x] Все команды пульта доступны в UART; действия панели и результаты
  зеркалируются полным многострочным выводом, без повторного исполнения
  и без засорения терминала фрагментами прокрутки HDSP.
- [x] Полный RT-11FB/UJLOAD regression на CP63: 42 cases + CTRL/C + cold reboot,
  counts/UART равны CP62; [исходники и результаты](tb/reports/cp63/) сохранены.
- [x] Пользователь подтвердил короткий RESET и прокрутку HDSP на плате CP64
  после физического отключения JTAG от общих пинов клавиатуры.
- [x] Пользователь подтвердил поведение после длинного RESET: ODT требует
  повторной активации, как предусмотрено контрактом cold reset CP63.
- [ ] Зафиксировать полный проход поклавишной раскладки; базовая работа
  пульта CP66 подтверждена пользователем ниже.
- [x] CP64: UJON проверяет код/wrapper/ABI, устанавливает debug-vector с readback;
  собственные согласованные START/STEP exits, старый UJLOAD не меняется.
- [x] CP64: полный RT-11 install/debug/continue/reset и запрос внутри RK/SD;
  83 checks, полный UART и 58 HDSP glyph frames PASS.
- [x] HG на CP67b до/после ODT: два HG→SD→HG переноса по 1024 байта,
  R/D/C между COPY, без перезапуска daemon/драйвера; обе копии совпали.
  [Аппаратный журнал и границы проверки](docs/board-hg-odt-cp67.md).
- [ ] Остановка посреди HG-транзакции, таймаут и восстановление протокола;
  успешные COPY до/после ODT между операциями этого не проверяют.
- [x] Возврат пульта после UNLOAD HG и остановки daemon: короткий RESET,
  A → NEXT → PREV → E; пользователь подтвердил работу 2026-09-14.
- [x] CP65: автопрокрутка туда/обратно с паузами, PREV/NEXT по командам,
  история 32 адресов, горячие клавиши 8–F. Без дополнительных ресурсов FPGA.
- [x] CP65: регистры по одному с ожиданием только на пульте; UART dump целиком.
- [x] CP65: программный STEP OVER через ограниченный цикл STEP, с проверкой
  адреса возврата и SP; вложенные/рекурсивные вызовы, отмена, WAIT, удержание.
- [x] Установить CP65 ODT/UJON через RT-11: обе обратные SD-копии совпали
  по SHA256, FRAM ready и debug enabled подтверждены. FPGA не перепрошивалась.
  [Журнал](docs/board-bringup-cp65.md).
- [x] Получить подтверждение базовой работы CP65 на плате: после запроса
  проверки короткого RESET и автопрокрутки пользователь сообщил «работает».
- [ ] Выполнить полный проход команд на физическом пульте CP65: навигация,
  регистры с ожиданием и клавиши 8–F, включая STEP IN/OVER.
- [x] CP66: четыре программные точки и временная RUN TO/STEP OVER с обычными
  IRQ. HALT-vector hook сохраняет USER BPT/trace; прежний `T limit` доступен явно.
  Ошибки записи удерживают патч до подтверждённого восстановления.
- [x] CP66: полный RT-11/FRAM regression — 296 проверок, 514657090 тактов;
  112 breakpoint, 187 core, 2699 panel checks. Согласованные ODT/UJON и архив.
- [x] Установить CP66 на плату: обратные SD-копии совпали по SHA256, ODT READY
  и проверка UJON/debug enabled подтверждены. [Журнал](docs/board-bringup-cp66.md).
- [x] Получить подтверждение базовой работы CP66 на плате: пользователь
  сообщил «работает!» после установки и предложения проверить RESET/кнопку 9.
- [x] Проверить P/Z/U/T CP66 на физической плате через UART: нечётный SP,
  вложенный JSR, WAIT/KW11-L, чтение SD через RT-11, короткий RESET при RUN,
  восстановление патчей и возврат в RT-11. [Аппаратный журнал](docs/board-uart-cp66.md).
- [x] Получить подтверждение работы пульта CP66: пользователь сообщил
  «пульт работает. после длинного reset - odt надо активировать заново».
- [ ] Выполнить отдельный проход P/Z/U/T через меню и клавиши пульта CP66.
- [ ] RAW HALT и явный I/O просмотр, автоповтор клавиш,
  перемещение курсора редактирования и дополнительные RGB состояния.
- [ ] Квалифицировать непрерывный UART burst: текущий wire test ~1,77 ms/символ;
  программный overflow/error recovery проверен отдельно.

[CP64: команды, готовые файлы и проверки](docs/odt-cp64.md).
[CP65: пульт, навигация и STEP OVER](docs/odt-cp65.md).
[CP66: точки останова и работа IRQ](docs/odt-cp66.md).

[Контракт и измерения CP63](docs/debug-cp63.md).

## CP62 — векторный файловый загрузчик завершён

- [x] ABI2 UJLOAD, загружаемый HALT helper, RAW read/write/checksum/zero,
  постоянный HALT-вектор и общий возврат модулей через 166.
- [x] 42 файловых/error cases, CTRL/C с двумя принятыми символами и без
  RX overwrite, повторная установка, cold reboot с сохранением слотов.
- [x] 17 boot + 17 helper cases × portable/vendor; HC1200 synthesis:
  **1229 LUT / 343 FF / 6 EBR / 32,087 MHz**, 1002 uwords.
- [x] Отклонить 32-word вариант, применить 8-word предел (copy 0,662 ms).
- [ ] Непрерывный UART burst/буферизация: успешный paced CTRL/C не доказывает
  приём произвольного потока 115200 baud. RTL UART имеет один holding register.
- [x] CP64: рабочий UART/HDSP/keyboard ODT как файл ABI2; FP11 остаётся отдельно.
- [ ] Отдельный аппаратный этап: JED CP62a, прошивка и проверка RT-11/UJLOAD
  на плате. Сейчас установлен CP56a; default профиль CP52a не менялся.


## Уточнение 2026-09-12: начальный пуск через HALT RAM

- [x] CP61: ROM копирует bootstrap и resident в HALT RAM; код HALT вызывает
  по вектору copier в USER RAM и запускает скопированный bootstrap через START.
  MOV/MTUS/MFUS, readback обоих переносов, вектора и собственный HALT stack.
- [x] CP61: удалить ненужные USER ROM overlays; проверить real HC1200 gate.
  **CP61g: 1229 LUT / 343 FF / 6 EBR / 32,087 MHz**, 1002 uwords,
  31,824 MHz/FRAM PASS. Свободны 51 LUT / 24 slices / 1 EBR / 22 uwords.
- [x] Проверить cold reset, порчу resident, ошибки записи обоих банков,
  реальные RT-11 boot/HBTEST/DIR, UART и сохранение контекста.
- [x] Ограничить прежние установочные инструкции HALT-режимом: UJLOAD ABI1
  отвергает CP61 до изменения верхней памяти. Старый CP60 сохраняется.
- [x] CP62: UJLOAD использует векторные вызовы, ABI2 и загружаемый HALT helper.
  Слоты ODT/FP11 отделены от bootstrap, HALT-вектор остаётся постоянным.
  Ready публикуется после payload/BSS/readback/wrapper.
- [x] CP62: дополнить службу readback/zero и raw доступом к последним 8 КиБ FRAM,
  используя уже имеющиеся HALT-only команды, без нового MMU.
- [x] CP62: уменьшить блок до 8 слов после измерения задержки IRQ;
  непрерывный UART burst остаётся отдельной проверкой выше.
- [x] CP64: UART/HDSP/keyboard ODT из RT-11; FP11 firmware остаётся в TODO.
  Новые профили пока не прошиты, плата остаётся CP56a.

[Архитектура и результаты CP61](docs/halt-boot-cp61.md),
[векторный файловый загрузчик CP62](docs/vector-loader-cp62.md).

## Выбранное направление 2026-09-12: HALT ВМ2 и FP11 firmware

Пользователь выбрал механизм HALT ВМ2 с входом эмуляции по FP11 вместо FIS.
FIS остаётся в микрокоде; PSW/IPL J-11 сохраняются, служебный режим отдельный.
Полные ODT и FP11 устанавливаются в верхнюю FRAM программой под RT-11.
Это расширение uJ11 без MMU, не стандартное пространство
J-11. [Организация](docs/service-bank-proposal.md), [реализованный CP57e](docs/service-bank-cp57.md).
CP57e: 1260 LUT / 342 FF / 6 EBR / 31,982 MHz; на плате остаётся CP56a.
Выбран следующий профиль CP58a: 1246 LUT / 342 FF / 6 EBR / 30,743 MHz,
1002 слова; обычные STEP/SEL004 paths проверены. CP58b отклонён по площади.
Следующий выбранный профиль CP59d: **1239 LUT / 343 FF / 6 EBR /
32,531 MHz**, internal и FRAM setup/hold PASS при проверочных31,824 MHz.
Плата остаётся CP56a. [Timing CP59](docs/timing-cp59.md).
Следующий профиль CP60b исправляет обнаруженные загрузчиком RK ошибки:
**1251 LUT / 343 FF / 6 EBR / 32,807 MHz**, gate31,824 PASS.
[Загрузчик](docs/service-loader-cp60.md), [RK recovery](docs/rk-recovery-cp60.md).

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
- [x] CP59d: закрыть internal timing с допуском OSCH/period jitter и
  FRAM setup/hold с заданными PCB budgets: gate31,824 MHz PASS.
  Physical SCK pulse width и PCB delays остаются аппаратной проверкой ниже.
- [ ] Завершить HALT семантику ВМ2: copy H/P tracking, вложенные входы,
  SEL174/274 для ошибок незавершённого входа. Внешний HALT/STEP реализован CP63.
- [ ] Сократить footprint служебного профиля перед новыми аппаратными функциями:
  CP59d освободил 7 LUT, остаются 41 LUT / 16 slices / 22 microinstructions;
  до исходной цели ≤1100 LUT нужно ещё убрать 139 LUT.
- [x] Перенести служебные команды ВМ2 с фиксированными R0/R5 и HALT-only
  aliases; successful paths/USER checks и обычный SEL004 fault path проверены.
- [x] Ввести FP11 dispatch и отсутствие эмулятора; тестовый handler CP57,
  FIS сохраняет текущий результат и CPI (исходный FIS вход не меняется).
- [x] Измерить отдельный минимальный профиль service bank поверх CP56a:
  вход/возврат, сохранение контекста без гостевого стека, межбанковый доступ.
- [x] Включить в первый gate установочный доступ из обычного режима:
  RT-11 должна заполнять пустой верхний банк без уже работающего HALT firmware.
- [x] CP60: реализовать RT-11 loader `.SAV` и формат отдельных ODT/FP11 файлов:
  блочное чтение средствами ОС, перенос, readback/checksum, ready последним.
  Проверить ошибки файла/диска, прерывание загрузки, повторную установку,
  независимые ready-флаги и запрет автоматической активации старой FRAM после reset.
  42 full RTL cases + CTRL/C + cold reset PASS; файлы обработчиков пока тестовые.
- [x] CP60: исправить RECALIBRATE/DI, гостевой CCLR без повторного IRQ и
  SD cleanup; воспроизвести ошибку `.READW` через оригинальный DM.SYS.
  Полный HC1200 CP60b gate PASS. Запас теперь 29 LUT / 11 slices / 22 uwords;
  до ≤1100 LUT нужно убрать ещё 151 LUT перед новыми аппаратными функциями.
- [x] Проверить bank/CS isolation, I/O, прежний RK/cold FB, терминальные
  service faults/IRQ/trace контракта CP57; получить full HC1200 synthesis.
- [x] RT-11 loader → ODT/FPP на реальной плате — CP67/79/80, конфигурации — CP82.
- [ ] Подтвердить PCB/SCK на плате; bounded external FRAM timing не является
  физическим измерением. Задача вынесена в актуальный список выше.
- [x] CP64: ODT в верхней FRAM, загружаемый из
  RT-11; прежний вариант полного ODT в EBR заменён этим решением.
- [x] CP64: два интерфейса общего ODT: UART и HDSP/20-key пульт.
  Адрес/данные, R0–R7/PSW, редактирование, START/CONTINUE/STEP.
  Дополнительные RGB состояния остаются отдельной задачей выше.
  Оба используют общий контекст и загружаются из RT-11 в служебную FRAM.
- [x] Зафиксировать [раскладку пользователя](docs/panel-keyboard.md):
  0–F, MEM/PREV/NEXT/ENTER; отдельной ESC нет.
- [x] Сохранить схему/PCB пользователя; сопоставить S1–S20 с KC0–KC4,
  TMS/TCK/TDI/TDO и таблицей текущего сканера, указав границы подтверждения.
- [x] CP64: OCT/HEX и меню; коды MEM/PREV/NEXT/ENTER переназначаются командой K.
- [ ] Подтвердить соответствие подписей клавиш кодам на физической плате.
- [x] CP64: HALT panel driver без IRQ/PNWAIT, debounce полного скана,
  общий ввод/зеркалирование; UART-only при занятых HG pins. Полный HG — выше.
- [x] Определён и реализован в RTL запрос HALT во время исполнения гостя:
  короткий RESET, CP63; электрическая проверка и прошивка пока впереди.
- [x] CP68: оценить управляющую часть FP11 как PDP-11 firmware в верхнем банке;
  полный эмулятор развивается по списку выше. Загрузка через RT-11 проверена
  в симуляции; FIS сохраняется, аппаратный FP11 не возвращается.

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

## Отложено: аппаратный/микрокодный FP11(A), история CP30–CP31

В CP31 FP11 удалён из рабочей сборки, включая
RTL доступа к FP state, opcode dispatch, reset hook и 33 microinstructions.
Не оставлять его за параметром, занимающим control store. Рабочая версия
возвращается к 954/1024×36 v12; свободно 70 слов. FIS сохранён полностью.
Эксперимент восстанавливается из коммита `d59f19c` и source snapshots CP30.
Новый программный модуль CP68 описан в начале TODO; нижеследующие цифры
относятся только к старому аппаратному эксперименту.

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
