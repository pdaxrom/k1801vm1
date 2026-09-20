# Implementation status, 2026-09-20

[Актуальная документация CP67b](README.md):
[эксплуатация](user-guide-cp67.md), [устройство](system-cp67.md),
[разработка и воспроизведение](development-cp67.md).

**Аппаратная установка:** CP67b записана и проверена во FLASH; RT-11FB и DIR
работают на плате. Программный FPP обновлён до CP80, ODT CP77 сохранён:
[текущая установка](board-fpp-cp80.md), [исходная CP67b](board-bringup-cp67.md). Ниже — результаты checkpoints
на момент их фиксации.

## CP81 — BASIC FIS/FPU

Оригинальный DEC BASIC-11 V2.1 собран для FIS single, FPU single/double.
Полный RTL исходных сборок: 22 checks, три набора по 29 числовых сравнений
и double — PASS; FPU single/double также прошли на физической плате,
включая ошибки/восстановление. Обычный CPU PSW 177776 и общий NXM assist
отложены, CP67b/CP80 не менялись.

У FIS выявлена несовместимость старого BASIC с расширенным стеком .SFPA
при обнаруженном FPP. Адаптер 26 байт, отдельный B81FIJ, следует документации
DEC. SIMH FIS-only и полный RTL с FPP (17 checks) — PASS. B81FIJ установлен
2026-09-20 отдельным файлом; SD readback, 29 сравнений и семь команд ошибок/
восстановления через UART — PASS. Все три FRAM-модуля сохранили STATUS
140407; picocom восстановлен, HG выгружен. [Детали и архив](basic-cp81.md).

## CP80 — PSW-операнды в программном FPP

FP11.MAC 00.12: обращения к 177776 обслуживаются через USER CPSW, T защищён, FPS обновляется независимо от явной записи CPU flags. Размещение 4614 байт HALT FRAM, +96 к CP79; FPGA без изменений. PSW: 712 cases / 25823 checks в sync/logic/vendor; прежняя матрица: 444420 sync и 215760 logic; RT‑11 99 checks — PASS. Установлен 2026-09-19: SD readback, cold init, 22 PSW/FPS шага, восстановление 39 слов и CPU-контекста, 43 STEP PC/PSW, FP dump и native FPTST — PASS. [Аппаратный журнал](board-fpp-cp80.md). [Контракт, проверки и ограничения](fpp-psw-cp80.md).

## CP79 — программная эмуляция FPP J‑11

FP11.MAC 00.11 следует руководству DCJ11: FIUV до выполнения, load flags по J11, jammed ADD/SUB. 4518 байт HALT FRAM, прирост FPGA нулевой. Sync 444420/30934144, logic 215760/15037488, directed portable/vendor 111/4174 каждый, RT‑11/ODT 99 checks — PASS. Установлен 2026-09-15: три SD readback, cold init всех модулей, 43 аппаратных STEP PC/PSW, FP dump и native FPTST — PASS. [Аппаратный журнал](board-fpp-cp79.md). [Границы совместимости и проверки](fpp-j11-cp79.md).

## CP78 — CPU-local PSW

Чтение/запись слова и байтов `177776/177777` реализованы в отдельном профиле `--psw-cp78`. T защищён, PS<10:9>=0, явная запись NZVC не затирается флагами команды. 1741 native cases / 12486 checks, portable/vendor; DFFPA/DFFPB/DFFPC — по одному проходу без ошибок и без test-only adapter. RT-11/UJMOD/43 ODT STEP/cold init/OFF — 99 checks PASS. Microstore прежняя: 1005 слов. CP78a/b/c не прошли MAP: 1290/1277/1286 LUT, 649/643/648 slices. PAR/TRACE отсутствуют; CP78 не установлен. [Границы и результаты](psw-cp78.md).

## CP77 — FP debugger и DEC diagnostics

ODT: все 4096 слов FP-диапазона, 1008 live-FPS сочетаний, 9262 checks. Пульт: 3324 checks / 116 окон; UART печатает все FP-регистры, панель ждёт между страницами. ODT/FP11 теперь занимают 13668/4560 байт; FP base 060000. Три оригинальные DEC FP11-A части: по одному проходу без ошибок с test-only PSW read. Без него DFFPA сообщает trap 4 на отсутствующем регистре 177776. Hardware CP67b прежняя, установка на плату не выполнялась. [Полные границы проверки](odt-fp-cp77.md).

## CP76 — преобразования FP11-A

46 мнемоник / 3949 корректных кодировок; все документированные семейства.
88 illegal-AC комбинаций и 59 reserved control кодов проверены отдельно.
4282 байта кода + 268 BSS/stack = **4550 байт HALT FRAM** (+1130).
Исправлены LDF/LDD flags при memory FIUV; остальные старые fixtures прежние.

Полный RTL регресс: **444444 cases / 30951840 checks — PASS**.
Logic: 215784 / 15052576; vendor: 1092 / 76360;
directed sync/vendor: 111 / 4174 каждый.
SPI FRAM: 5 / 1144, 403 измерения; numeric model: 251328 comparisons.
Native RT-11/UJMOD/43 STEP: **94 checks / 811627733 clocks /
9429 UART bytes — PASS**. 53 обычных hardware inputs CP67b прежние.
FPGA/плата не менялись; FP11.BIN физически не установлен. Внешние DEC
FP diagnostics ещё не пройдены. [Отчёт](fp11-conversions-cp76.md).

## CP75 — MODF/MODD в программе FRAM

32 мнемоники / 2429 корректных кодировок, 72 illegal-AC сочетания.
3154 байта кода и 266 байт состояния/стека, всего 3420 (+400 к CP74).
Семантика FP11-A с 59-битным произведением D; оба результата, чётные/
нечётные AC, все addressing modes, flags и ошибки. Преобразований пока нет.

Полный RTL регресс: **225084 cases / 15632368 checks — PASS**.
Logic: 81544 / 5707456; vendor: 276 / 19712; directed sync/vendor:
49 / 1742 каждый. SPI FRAM: 4 / 848, 259 измерений. Native RT-11/UJMOD/
29 STEP: **80 checks / 689803088 clocks / 6825 UART bytes — PASS**.
200000 Python/C128 и 200000 original MOD helper comparisons.
Все 53 обычных hardware inputs CP67b совпали по SHA256; нового synthesis
нет. На физическую плату FP-пакет не установлен.
[Семантика и результаты](fp11-mod-cp75.md).

## CP74 — MUL/DIV F/D в программе FRAM

30 мнемоник / 2181 корректная кодировка, 64 illegal-AC комбинации.
2754 байта кода, 3020 байт с BSS/stack (+510 от CP73). Умножение за
24/56 итераций, деление за 31/63; общие округление/упаковка ADD/SUB.
Новые uint128/Fraction/serial модели: по 200000 сравнений; 155520 точных
границ ошибки. Все 149404 fixtures CP73 сохранены, добавлены 44992.
Sync: 194396 cases / 13529088 checks; logic: 64248 / 4519200;
vendor: 304 / 21764; directed sync/vendor: 43 / 1527 каждый; SPI FRAM:
4 / 812 — PASS. RT-11/UJMOD/22 ODT STEP и самопроверка: 66 checks /
628973209 clocks / 5523 UART bytes — PASS.
Hardware inputs CP67b прежние (53 SHA256), прирост LUT/FF/EBR/uwords — 0.
Пакет FP11.BIN на физическую плату ещё не установлен.
[Семантика, ограничения и измерения](fp11-muldiv-cp74.md).

## CP73 — ADD/SUB F/D в программе FRAM

26 мнемоник / 1685 корректных кодировок, 48 illegal-AC комбинаций.
2252 байта кода, 2510 байт с BSS/stack. Семь guard bits FP11-A,
округление/усечение, нормализация, FIV/FIU/FIUV/FID и все modes.
ABS/NEG теперь завершают запись нуля перед FIUV exception.
Sync: 149404 cases / 10426000 checks, logic: 46040 / 3254816 — PASS.
Vendor: 472 / 33088, directed sync/vendor: 39 / 1389 каждый, SPI FRAM:
4 / 732; RT-11/UJMOD/ODT: 56 checks / 592741136 clocks / 4779 UART bytes
(18 FP STEP, самопроверка, cold init и OFF) — PASS.
Аппаратные inputs CP67b совпали по 53 SHA256; новых LUT/FF/EBR нет.
Физическая плата прежняя, FP11.BIN не установлен.
[Точные границы проверки и измерения](fp11-arithmetic-cp73.md).

## CP72 — прямые переносы FP11 AC

Mode-0 LDF/LDD копируют напрямую без FBUF, STF/STD используют общий
фиксированный перенос 2/4 слов. LDF/LDD AC5→AC2: **17662/18120 clocks**,
на 17–19% короче CP71; STF/STD AC2→AC5: **16845/17303**, выигрыш 6–7%.
Другие 165 SPI measurements прежние. **1394 байта кода / 1624 байта FRAM**,
+30 байт кода; BSS/stack и hardware CP67b не менялись.

99104 / 6947312 sync и 22748 / 1621848 logic — PASS на прежних векторах.
Directed sync/vendor: 35 / 1254 каждый, включая IRQ/ODT между записями AC;
SPI FRAM: 4 / 632 — PASS. Vendor: **467 cases / 32176 checks**, 128 manual — PASS. RT-11: **43 checks / 531124415 clocks / 3476 UART bytes — PASS**.
ISA и ограничения CP71 сохранены. На плату пакет не установлен.
[Контракт оптимизации, остаточная регрессия и пакет](fp11-paths-cp72.md).

## CP71 — FP11-A unary и compare

CLR/TST/ABS/NEG/CMP F/D со всеми addressing modes, AC0–AC5 и exceptions.
Всего **1189 корректных кодировок**, 32 illegal-AC комбинации; **1364 байта
кода / 1594 байта полной аллокации**. Sync 99104 / 6947312, из них
97176 differential и 1928 явно отдельных ожиданий. Logic 22748 / 1621848;
directed sync/vendor 33 / 1172 каждый; SPI FRAM 4 / 632, 169 измеренных операций.
Vendor: 1347 cases / 96842 checks — PASS. RT-11/UJMOD/ODT: **43 checks / 531033015 clocks /
3476 UART bytes — PASS**. TST реализует FP11-A flags-before-UV; J-11
отличается. Hardware CP67b совпал по 53 обычным synthesis inputs,
прирост LUT/FF/EBR нулевой. Модуль не установлен на физическую плату.
ADD/SUB/MUL/DIV и преобразований ещё нет; замедление переносов от CP70
задокументировано. [Контракт, проверки и пакет](fp11-unary-cp71.md).

## CP70 — FP11 F/D transfers

LDF/LDD и STF/STD, все modes, AC0–AC5, FIUV/FID и faults; всего 693 корректные
кодировки, ещё 16 invalid-AC комбинаций явно дают FEC=2. **1022 байта кода /
1250 байт полной аллокации**. 41812 cases / 2961696 checks, из них 136
отдельных ожиданий для двух найденных дефектов неизменённого DCJ11 oracle.
Logic: 12080 / 862956. Directed sync/vendor: 31 / 1110 каждый. SPI FRAM:
4 / 450, 79 измеренных операций. Vendor transfers: 445 / 31814. Полный
RT-11/UJMOD/ODT: 48 checks / 521238827 clocks / 3285 UART bytes — PASS.
Аппаратные inputs CP67b побайтно прежние,
прирост LUT/FF/EBR нулевой. Модуль не установлен на плату, арифметики ещё нет.
[Полный контракт и результаты интеграции RT-11](fp11-transfers-cp70.md).

## CP69 — FP11 status addressing

Все режимы LDFPS/STFPS/STST, включая R6/R7 и ошибки: **8 мнемоник /
197 кодировок**, 734 байта кода и **950 байт полной аллокации** в HALT FRAM.
5012 differential cases / 348576 checks; прежние faults/IRQ/trace/ODT
сценарии сохранены. SPI FRAM: 4 scenarios / 366 checks. Полный RT-11/UJMOD/ODT:
**33 checks / 503618729 clocks / 2921 UART bytes — PASS**.
Hardware CP67b побайтно прежний; дополнительных LUT/FF/EBR нет.
LDF/STF и арифметика ещё не реализованы. FP11.BIN не установлен на плату.
[Контракт, ограничения и benchmark с регрессией register path](fp11-memory-cp69.md).

## CP68 — управляющая часть FP11 firmware

Семь мнемоник / 21 кодировка, FPS/FEC/FEA, шесть 64-bit AC и частный stack
в HALT FRAM: **456 байт кода, 668 байт полная аллокация**. Полная FP11 ISA
ещё не реализована; FIS и hardware CP67b сохранены. Прирост FPGA нулевой.
90 112 differential cases; 28 directed scenarios для faults/IRQ/trace/debug;
4 SPI FRAM scenarios / 318 checks для cold init, совместимости ODT/SDBOOT
и measurements. RT-11/UJMOD/ODT regression: **24 checks / 460895201 clocks /
1982 UART bytes**, включая установку, STEP, повторный reset и OFF — PASS.
Это результат симуляции; новый FP11.BIN **не установлен на физическую плату**.
[Контракт, точная ISA и измерения](fp11-firmware-cp68.md).

## CP67 — retained FRAM modules

Общая таблица из четырёх слов, immutable checksum и cold init по BASE.
ODT сам восстанавливает данные/векторы и включает debug; простой SDBOOT —
такой же заменяемый модуль. UJMOD публикует VALID после полного readback.
UART ESC до первого FRAM-вызова выбирает встроенный bootstrap, независимо
от правильности кода расширенного модуля. Образы абсолютные, relocation пока нет.

CP67b MAP/PAR/TRACE: **1244 LUT, 381 FF, 7 EBR, 32,032 MHz**, 1005 uwords,
31,824 MHz + FRAM timing PASS. JED записан во FLASH с Verify; RT-11FB и DIR PASS.
Portable/vendor cold tests: 30 cases / 249 checks каждый; native loader:
38 cases / 49 checks; ODT: 187 core / 112 breakpoints / 2699 panel checks PASS.
Полный RT-11 install/reset/recovery regression: 55 checks / 1 023 462 941 такт /
3983 байта UART — PASS. Исходники, native assembly и сырые логи архивированы;
ODT.BIN, SDBOOT.BIN и UJMOD.SAV установлены с полным обратным чтением.
На плате после длинного RESET оба модуля получили `140407`; короткий RESET,
ODT R/D/C и возврат в RT-11 проверены. Позднее прошёл отдельный
[аппаратный ESC recovery](board-recovery-cp67.md): RT-11 с обходом ODT,
сохранность таблицы, возвращение ODT после обычного RESET, R/D/C и DIR.
Баннер recovery записан; последующий обычный RESET подтверждён пользователем,
его результат проверен по ODT. Журналы и SHA256 — по ссылкам.
[HG до/после ODT](board-hg-odt-cp67.md): оба переноса по 1024 байта совпали
побайтно без перезапуска HG. После UNLOAD HG и остановки daemon пользователь
2026-09-14 подтвердил возврат пульта: RESET, A/NEXT/PREV/E, R0/R1/R0 и RT-11.
Подтверждение пульта сохранено отдельно от семи проверок UART-архива.
[Контракт, файлы и ограничения](retained-modules-cp67.md).

## CP66 — программные точки и STEP OVER с обычными IRQ

Четыре постоянные HALT-точки и одна временная, RUN TO, STEP OVER с проверкой
PC/SP, останов коротким RESET. USER BPT/trace vector не изменяется, вызов
штатного загрузчика разоружает патчи и сохраняет его ABI. Ошибки установки
откатываются; невосстановленный патч/вектор запрещает продолжение.
UART P/Z/U и новые пункты меню MEM; кнопка 9 — новый T, `T limit` — прежний цикл.

**10962-byte payload, 11746-byte allocation, 542 байта свободны.**
PASS: 112 breakpoint checks, 187 прежних CPU checks, 2699 panel checks;
полный RT-11/FRAM/SD/KW11-L тест — 296 проверок, 514657090 тактов,
3038 UART bytes, 80 HDSP frames, RX overrun=0 при заявленном темпе ввода. RTL/ROM/микрокод равны CP63b по SHA256; прирост ресурсов FPGA нулевой.
CP66 установлен на плату: обе SD-копии совпали по SHA256, HALT FRAM ready
и проверка UJON/debug enabled подтверждены. Пользователь подтвердил базовую
работу CP66 сообщением «работает!». Полный проход отдельных команд пульта
остаётся отдельной аппаратной проверкой.
[Журнал установки](board-bringup-cp66.md).
Отдельно прошли [аппаратные UART-проверки](board-uart-cp66.md): STEP/P/Z/U/T,
нечётный SP, вложенный JSR, WAIT/KW11-L, чтение ODT.BIN через RT-11,
отмена RUN коротким RESET, восстановление патчей и штатный возврат в RT-11.
Сохранены сырые журналы и проверяемый manifest 15 сценариев; меню пульта
в этом проходе не проверялось. После завершения debug включён, UART/HG свободны.
Затем пользователь подтвердил работу пульта и необходимость повторной
активации ODT после длинного RESET. Последнее соответствует контракту CP63:
cold reset снимает ready/debug enable; это подтверждение пользователя,
без новой UART-записи и полного протокола нажатий клавиш.
[Контракт и воспроизведение](odt-cp66.md).

## CP65 — программное управление пультом и STEP OVER

Автопрокрутка длинных строк туда/обратно с паузами, PREV/NEXT по известным
границам команд, история 32 адресов и горячие клавиши 8–F. Пульт ждёт на каждом
регистре; UART печатает весь набор без ожидания. Удержание 8/9 не повторяет шаг.
STEP OVER — ограниченный цикл STEP для JSR до совпадения адреса возврата и SP;
вложенные и рекурсивные вызовы проверяются, IRQ/trace остаются отложенными.

**9118-byte payload, 9902-byte allocation, 2386 байт свободны в ODT slot.**
Дополнительных LUT/FF/EBR нет; hardware CP63b не менялся. Готовые файлы
собираются согласованной парой ODT/UJON. CP65 установлен на плату:
обе SD-копии совпали по SHA256, FRAM ready и debug enabled подтверждены.
[Аппаратный журнал](board-bringup-cp65.md): пользователь подтвердил работу CP65
после запроса проверки короткого RESET и автопрокрутки. Полный проход всех
команд физического пульта остаётся отдельной проверкой.
[Подробности и результаты проверки](odt-cp65.md).

## CP64a — граница прокрутки ODT

После длинного результата короткая строка больше не прокручивается в остатки
текста за NUL. **7632-byte payload, 8416-byte allocation**, прирост 6 байт,
RTL/микрокод/FPGA без изменений. Проверены все смещения строки с фотографии,
поток двух HDSP, границы, удержание и переназначение клавиш, переход ENTER,
сохранность контекста и отсутствие USER memory transactions при прокрутке.
[Подробности и воспроизведение](odt-cp64a.md).

## CP64 — загружаемый UART/HDSP ODT

Production `ODT.BIN` и проверяющий активатор `UJON.SAV`, ABI2 loader не изменён.
R0–R7/PSW, USER RAM word/byte с readback, START/STEP/WAIT, disassembler integer/
EIS/FIS/SPL, меню и ввод 20-key панели, 16-character window с прокруткой.
Все действия пульта доступны через UART и зеркалируются туда целиком.
Отмена очищает незавершённый ввод; overflow отбрасывается до нового CR.

**7626-byte payload, 8410-byte allocation, 3878 байт свободны в ODT slot.**
RTL/ROM/microstore точно равны CP63b по SHA, поэтому LUT/FF/EBR/Fmax прежние.
187 checks × logic/sync/Lattice ROM, быстрый electrical-panel test и полный
cold RT-11 → UJLOAD → UJON → stop/step/edit/panel/continue → re-enable/cold boot:
**83 checks, 331098776 clocks, 1531 UART bytes, 58 HDSP frames — PASS.**
Проверены останов внутри RK/SD с отложенным входом, ошибка активации испорченного
кода, arbitrary R4, odd SP под IPL7, UART/panel input ownership и glyph bitstream.

Не прошивалось на плату (CP56a), default остаётся CP52a. Отдельно впереди:
проверка RESET circuit/кодов клавиш на плате, breakpoints/STEP OVER, RAW HALT/I/O,
автоповтор, история disassembly и полный HG-сеанс. Полный FP11 отсутствует.
[Контракт и воспроизведение](odt-cp64.md), [verification](verification-cp64.json).


## CP63 — кнопка и аппаратный debug-вход

Профиль `--debug-cp63` реализует независимый short/long RESET controller,
внешний HALT по вектору 110/112, STEP с повторным входом, сохранение WAIT
и deferred trace. CONFIG bit2 явно включает расширение после установки
обработчика; прежний UJLOAD его не включает, native HALT по 170 сохранён.

Выбран **CP63b: 1230 LUT / 381 FF / 6 EBR / 618 slices / 32,246 MHz**,
1005 microinstructions; gate 31,824 MHz + FRAM PASS. Старые 1002 microinstructions,
opcode decoder и firmware ROM побайтно сохранены. Новых EBR нет.

19 CPU cases × 3 ROM/decode режима, 28 button checks × HOLD10/100/128,
два actual SPI FRAM cold/upload/button/step/reset прогона и четыре behavioral
negative controls PASS. Final EDIF: 3088 nets, без конфликтов/необъяснённых
floating; core lint без новых diagnostics, button strict lint PASS.
Полный RT-11/UJLOAD regression: 42 cases, CTRL/C, cold reboot PASS.
971335085 clocks, 16863737 retired, 232675 upper writes и 3713 UART bytes
совпадают с CP62. [Архив точных исходников и результатов](../tb/reports/cp63/),
[verification](verification-cp63.json), [synthesis](synthesis-cp63.json).

Это основа для отладчика; штатные UART/HDSP UI, disassembly и breakpoints
ещё не реализованы. Активация debug-вектора в установщике и STEP-выход монитора
требуют следующего программного этапа. Проверка электрической цепи RESET
и активного отладчика с RK/SD/HG на плате остаются отдельными проверками.
Плата не прошивалась: CP56a, default CP52a.
[Контракт CP63](debug-cp63.md), [полный план отладчика](odt-debugger-plan.md).

## CP62 — файловый загрузчик через постоянный HALT-вектор

Завершён UJLOAD ABI2: USER файловые вызовы RT-11, загрузка HALT helper
в FRAM и обращения через vector 170. Первые 4 КиБ верхнего банка системные,
ODT получает 12 КиБ, FP11 — 48 КиБ, включая physical I/O backing для данных.
Return ABI модулей — JMP @166. Полных ODT и FP11 файлов пока нет.

Реальный synthesis **CP62a: 1229 LUT / 343 FF / 6 EBR / 32,087 MHz**,
1002 microinstructions; gate 31,824 MHz/FRAM PASS. Ни одна аппаратная
функция не добавлена: изменились только 31 слово firmware ROM. Helper
456 байт находится в UJLOAD.SAV (4096 байт), не в EBR. Плата не прошивалась.

Финальный full RTL прогон: **42 cases + CTRL/C + cold reboot PASS**,
971335085 clocks, 16863737 retired, 3713 UART wire bytes. Сравниваются
все 65536 байт верхнего банка, кроме аппаратного CPC/CPSW. Проверены
file/range/checksum/padding, SD failure, write protection, поздняя порча,
возврат HALT/FP11/SEL004, сохранение другого модуля, повторная установка.
После cold reset RT-11 возвращается, слоты сохраняются, ready=0.
17 boot cases + 17 helper cases × portable/vendor также PASS.

Первый вариант с 32 словами не прошёл CTRL/C. При пределе 8 слов
максимальный copy занимает 19571 clocks (~0,662 ms); полный тест прочитал
оба CTRL/C без RX overwrite и сохранил другой модуль. Непрерывный UART
поток всё ещё требует отдельного измерения. 6 ABI2 host tests и 4 clock
checks PASS. [Описание и воспроизведение](vector-loader-cp62.md),
[точные источники и логи](../tb/reports/cp62/).


## CP61 — начальный HALT-пуск и bootstrap из USER FRAM

ROM сначала копирует SD bootstrap и resident в HALT RAM. Далее код в HALT
вызывает через `000160` подпрограмму MTUS/MFUS, проверяет копию в USER RAM
и выполняет START на `004000`. Старые USER ROM overlays убраны.
28-byte первый вариант заменён окончательной ROM-процедурой 50 байт;
resident 164 байта, всё помещается в прежний firmware EBR.

Выбран **CP61g: 1229 LUT / 343 FF / 6 EBR / 616 slices / 32,087 MHz**;
1002 microinstructions, 31,824 MHz internal/FRAM PASS. На 22 LUT меньше CP60b.
17 cold/copy cases × 2 ROM-модели и два RT-11 cold boot с вызовами из SAV
прошли на CP61e. Для финального CP61g: исчерпывающий dispatch proof,
побайтное равенство остального RTL/ROM, отдельный RT-11/HBTEST/DIR прогон,
отказ старого UJLOAD до записи HALT RAM, RK CSR и final EDIF audit — PASS.

CP61 меняет размещение resident: UJLOAD ABI1 ещё не перенесён на новый
векторный протокол, полные ODT/FP11 пока отсутствуют. Установочные команды
доступны только в HALT. Плата CP56a, default CP52a; CP61 не прошивался.
[Подробности и воспроизведение](halt-boot-cp61.md), [verification](verification-cp61.json).

## CP60 — RT-11 loader и исправление RK recovery

Готов `UJLOAD.SAV`: оригинальные MACRO/LINK V5.03, 0 ошибок, 7 блоков.
Формат ABI1 проверяет границы, entry/fault, размер файла, checksum и padding;
перенос и BSS проверяются чтением, ready выставляется последним.
42 полных RTL-сценария, CTRL/C и cold reset PASS; обычный DCJ11 в SIMH
корректно отвергается. Проверенные ODT/FP11 файлы — тестовые обработчики,
полные монитор и эмулятор пока не реализованы.

Исправлены RECALIBRATE/DI, CCLR без лишнего IRQ и освобождение SD после
ошибки. Настоящий DM.SYS выполняет восемь повторов и возвращает ошибку
загрузчику; дальнейшая загрузка работает. 28 CSR beats × 2 модели и
negative control, 12 прежних service cases × 2, девять benchmarks × 2 PASS.
CPU/FIS и 1002 слова микрокода не изменены.

Полный **CP60b: 1251 LUT / 343 FF / 6 EBR / 629 slices / 32,807 MHz**.
Gate 31,824 MHz с FRAM constraints PASS. Осталось 29 LUT / 11 slices /
1 EBR / 22 microinstructions; CPU/SCK номинально 29,56 MHz.
Профиль `--rk-cp60`, default CP52a и установленный CP56a прежние.
[Загрузчик](service-loader-cp60.md), [RK recovery](rk-recovery-cp60.md),
[verification](verification-cp60.json).

Ниже — история checkpoints.

## CP59d — timing исправлен, семантика CP58a сохранена

**1239 LUT / 343 FF / 6 EBR / 624 slices / Fmax 32,531 MHz**, 1002 слова.
Internal 31,824 MHz и внешние FRAM setup/hold прошли. Доказана ненаблюдаемость
текущего bus_error в CJUMP; настоящий fault redirect/repair сохранён.
CS output FF продублирован в PIO с теми же входами и без дополнительного такта.

91 CPU × 3, 12 board × 2, 23840 FIS portable + 645 vendor, девять
benchmarks × 2, formal с двумя negative controls, EDIF с тремя — PASS.
Cold RT-11FB + DIR: 173379163 clocks, UART совпадает с CP56.
Свободны 41 LUT / 16 slices / 1 EBR / 22 слова. Запас остаётся мал;
полные ODT/FP11 и RT-11 loader пока не реализованы.
PCB budgets и физическая ширина SCK требуют аппаратной проверки.
CP59d opt-in; default CP52a и плата CP56a прежние.
[Подробности](timing-cp59.md), [verification](verification-cp59.json).

Ниже — история checkpoints.

## CP58 — STEP/SEL004, выбран CP58a после полного synthesis

91 CPU cases × 3 режима ROM/декодера, 12 full-board service cases × 2,
23840 FIS portable + 645 vendor и девять benchmarks × 2 проходят.
Cold RT-11FB + DIR: 173379163 clocks, UART совпадает с CP56.
STEP пропускает одну проверку IRQ/T; MFUS/MTUS при ошибке имеют точный
R5 delta и используют SEL004 без перезаписи CPC/CPSW. Ошибка незавершённого
входа/вектора пока терминальна; SEL174/274 и внешний HALT ещё не реализованы.

1002 microinstructions, **1246 LUT / 342 FF / 6 EBR / 30,743 MHz**,
627 slices. Свободны 34 LUT / 13 slices / 1 EBR / 22 слова. Полный
MAP/PAR/TRACE PASS на номинальных 29,56 MHz; final EDIF: 3175 nets,
нет конфликтующих драйверов/необъяснённых floating nets, три negative controls.
CP58b (одно слово SEL004) дал 1258 LUT / 30,626 MHz и отклонён.
До прошивки остаются OSCH tolerance и external pin timing. CP58 opt-in;
плата CP56a и default CP52a прежние. [Контракт](service-bank-cp58.md),
[проверки](verification-cp58.json).

## CP57 — минимальный служебный банк, отдельный профиль

CP57e прошёл полный HC1200 MAP/PAR/TRACE: **1260 LUT / 342 FF / 6 EBR /
31,982 MHz**, 1000 microinstructions. Входы HALT/FP11, frozen CPC/CPSW
в верхней FRAM, START, HALT-only aliases и установочный доступ из обычного
режима проверены. PSW16, FIS и общий I/O сохранены; MMU отсутствует.

44 CPU cases × 3 ROM/decode modes; 10 full-board service cases × portable/
vendor; 23840 exact FIS portable + 645 vendor; на CP57d полный набор 23840
пройден в обоих симуляторах. Девять workloads × 2 сохранили все counters CP56.
Cold RT-11FB + DIR: 173379163 clocks, те же 3270 raw UART bytes.
18 microassembler + 5 FIS/linker unit tests PASS. EDIF: 3180 nets без
конфликтующих драйверов и необъяснённых floating nets.

Пока нет STEP/SEL004 fault completion, непрерывного H/P copy tracking,
RT-11 `.SAV` loader, полных ODT/FP11 и аппаратной проверки этого профиля.
Свободны 20 LUT/5 slices: ресурсный запас мал. На плате по-прежнему CP56a,
default по-прежнему CP52a. [Контракт CP57](service-bank-cp57.md),
[verification manifest](verification-cp57.json).

## CP56 — ускорение SPI FRAM, проверки и synthesis PASS

На frozen CP54b реализован SCK 29,56 MHz через ODDRXE, CPU 29,56 MHz.
4096 random и 654 directed операций, 7600 beats с задержками/4480 reset
позиций, 9 portable + 9 vendor workloads и cold RT-11FB + DIR прошли.
R,R: 40,0625 → **23,5625 CPI**. Cold: 288686609 → **173379163 clocks**.
Полный HC1200 gate: **1184 LUT / 339 FF / 6 EBR / 31,996 MHz**.
Дополнительный FRAM TRACE с PCB budgets и четыре clock-corner tests
(ещё 3800 beats / 2240 reset offsets) прошли. Уточнённый OSCH envelope
+5,5%, 43/57 и 2% jitter оставляет **0,147 ns** запаса длительности SCK;
physical pulse-width signoff ожидает измерения или увеличения запаса.
CP56a установлен для аппаратной проверки по запросу пользователя;
default сборки остаётся CP52a.
[CP56](spi-cp56.md), [synthesis](synthesis-cp56.json).

## Текущая плата — CP56a

По запросу пользователя JED CP56a экспортирован из проверенной
разводки и записан во FLASH HC1200: Erase/Program/Verify PASS. На UART
появились RT-11FB V05.03 и prompt, picocom восстановлен. Штатная частота
CPU и FRAM SCK 29,56 MHz; архивный Fmax 31,996 MHz. Default RTL пока CP52a.
Программы и периферию на новой прошивке проверит пользователь.
[JED, hashes и журнал](board-bringup-cp56a.md). Предыдущая установленная
прошивка [CP54b](board-bringup-cp54b.md) сохранена для возврата.

Ниже — результаты checkpoints до этой аппаратной установки.

## CP55 — native shared RX, проверки и synthesis PASS

На frozen CP54b приёмный shift register совмещён с `rdata[15:8]`:
busy high data меняются, valid data/SPI/ACK сохраняются. Два induction
proofs, formal и executable mutations, X/Z/reset/128 КиБ FRAM, девять
portable и девять vendor workloads, новый cold FB+DIR PASS. Все counters
и raw UART совпали с CP54b. Полный MAP/PAR/TRACE: **1182 LUT / 333 FF /
6 EBR / 593 slices / 30,827 MHz**. От CP54b **−3 LUT / −8 FF**, но
Fmax ниже на 1,446 MHz; slack 1,390 ns при 29,56 MHz. Свободны 98 LUT /
47 slices / 1 EBR, до <=1100 ещё 82 LUT для CP55a. После сравнения пользователь
выбрал **CP54b основой дальнейшей работы**: 1185 LUT / 341 FF / 6 EBR /
32,273 MHz, slack 2,843 ns; до <=1100 ещё 85 LUT. CP55a остаётся экспериментом.
Source/report hashes и EDIF проверены.
Default CP52a, MMU-ветвь и плата CP29a прежние. [CP55](rx-cp55.md).

## CP54 — decode/ACK, выбран CP54b после synthesis

Вынесен независимый от адреса RK DMA operand; второй вариант также
объединяет быстрые ACK перед общей I/O qualification. Два SAT proofs
всех 39 output bits, три negative controls, 524288 X/Z-data cases,
86 board beats и 36 portable/vendor workloads PASS. Оба новых cold FB+DIR
PASS, по 288686609 clocks, все counters/raw UART совпали с CP53a.
Оба full-board synthesis PASS: A — 1192/341/6/31,524 MHz,
B — **1185/341/6/32,273 MHz**. Выбран B, −10 LUT от CP53a; свободны
95 LUT/45 slices/1 EBR, slack 2,843 ns при 29,56 MHz. Source/report hashes
и конечные EDIF проверены. Default/MMU/плата прежние; до <=1100 LUT
ещё нужна оптимизация. [CP54](ack-cp54.md).

## CP53 — cursor mapping, synthesis и выбранный board PASS

Подготовлены три варианта CP52b: XOR/AND increment, grouped comparator,
оба вместе. Шесть положительных SAT proofs и три negative controls,
12288 random/1962 directed FRAM операций, 129 board beats, 27 full-board
workloads; все benchmark counters совпали с CP52. Mapped EDIF CP52b
подтвердил 8+5 CCU2D; проверка конечных сетей не нашла multiple strong drivers.
CP53a/b/c: **1195/1204/1208 LUT**, по 341 FF и 6 EBR,
Fmax 31,338/30,865/31,788 MHz, все routed/timing PASS. Выбран A:
−3 LUT от CP52b, свободны 85 LUT/38 slices/1 EBR, slack 1,919 ns.
Девять новых vendor EBR workloads и cold FB+DIR прошли; counters/raw UART
точно совпали с CP52. Default RTL, MMU-ветвь и плата не менялись;
цель <=1100 LUT ещё требует оптимизации. [CP53](cursor-cp53.md).

## CP52 — native sequential FRAM candidate, simulation и synthesis PASS

Новый 15-bit cursor позволяет продолжать demand READ без повторной команды
и адреса. Board decoder исключает ROM/CSR/private RK DMA; записи, byte/odd,
bank boundary и reset проверены. Полный board с portable/vendor EBR:
R,R **40,0625 CPI (2,671×)**. Cold RT-11FB + DIR **288686609 clocks (1,229×)**;
каждый FRAM beat проверен по модели, UART wire/SD writeback/IRQ прошли.
Baseline с исправленным SL-prompt testbench повторил 354938300 clocks.
Сохранены source snapshots и logs. CP52a — 1159 LUT / 326 FF / 6 EBR /
31,470 MHz; CP52b — 1198 / 341 / 6 / 30,044 MHz, оба routed/timing PASS.
Цена ускорения +39 LUT/+15 FF; у candidate остаются 82 LUT и slack 0,544 ns.
Он сохранён отдельно до уменьшения площади; default, MMU-ветвь и плата
остаются прежними. Измеренный default baseline теперь CP52a.
[Детали CP52](fram-sequential-cp52.md).

## CP51 — ресурсный и performance baseline MMU-less board

Проверены архивные CP40h source/MAP/TRACE hashes и неизменность текущего
native RTL после CP50. Свободны 121 LUT / 56 slices / 1 EBR; pins заняты,
FF не являются главным ограничением. Critical path 31,802 ns проходит
через RF/address, board decode/ACK и microsequencer. Microstore имеет
70 свободных слов в 46 участках, максимум 3 подряд.

Девять новых полных board microbenchmarks прошли; register CPI 107,
FRAM busy 96,26%. Первый performance кандидат — sequential instruction
stream на FRAM, с отдельным полным resource/correctness gate. RTL,
MMU-ветвь и физическая плата не менялись; новой площади/Fmax нет.
[Подробный аудит](resources-cp51.md).

## CP50 — MMU-less по умолчанию

Решение от 2026-09-11: прекратить дальнейшее уменьшение площади ради MMU.
Рабочая ветвь без `UJ11_MMU` сохраняет CP40h; явный `MMU=1` сохраняет
незавершённый CP47c. Новых функций MMU нет, RT-11XM не проверена.
96 сравнений после препроцессора с архивами подтвердили обе ветви;
FRAM, native bus и CPU relocation/edges на portable/vendor прошли.
Default cold RT-11FB + DIR повторил CP40h: 354938300 clocks, прежние counts
и UART. В default elaborated hierarchy нет MMU-модулей.
Default профиль позднее заново синтезирован как CP52a; MMU gate не повторялся.
[Профили и проверки](build-profiles-cp50.md).

Ниже — история checkpoints; их будущие MMU-планы теперь отложены.

## CP49 — binary/LSB FRAM-варианты отклонены по площади

Original encoding, split-low и bit equations прошли шесть SAT proofs,
18 unit runs, три ошибочных перехода обнаружены proof и simulation.
CPU/vendor/edges/bus каждого варианта и cold FB split-low прошли;
counts/UART прежние. Атрибут syn_encoding проверен по установленному
Synplify Attribute Reference, September 2024, pp. 67–70.
Измерены четыре полных board gates. Контроль 1297 LUT / 351 FF / 7 EBR /
650 slices повторил CP47c. Original: 1325 / 351 / 7 / 664; split-low:
1335 / 351 / 7 / 669; equations: 1310 / 363 / 7 / 657. Все MAP FAIL,
PAR/TRACE/Fmax отсутствуют. Original коды сохранены, четыре state FF;
equations перекодированы в 16-bit one-hot. Final EDIF A–D: 3504/3502/3454/
3369 nets, направленных конфликтов или необъяснённых floating inputs нет.
Не приняты: CP47c/production/APR/FPGA сохранены; MMU/XM scope прежний,
превышение лучшей основы остаётся 17 LUT / 10 slices.
[Отчёт](area-fram-binary-cp49.md), [manifest](verification-cp49.json).

## CP48 — state encoding FRAM не уменьшил площадь

Контроль 1297 LUT / 351 FF / 7 EBR / 650 slices воспроизвёл CP47c.
Successors: 1319 / 357 / 7 / 661; onehot: 1313 / 357 / 7 / 658.
Оба отклонены, все три gates MAP FAIL, Fmax нет. Synplify распознал FSM
и применил one-hot recoding; итоговая схема больше исходного счётчика.
Четыре formal proofs, 12 unit runs, обе намеренные ошибки обнаружены;
CPU/vendor/edges/bus обоих вариантов и cold FB successors прошли.
Final EDIF A/B/C: 3504/3483/3498 nets, направленных конфликтов или
необъяснённых floating inputs нет. Основа CP47c, production/APR и плата
сохранены. До вместимости ещё 17 LUT / 10 slices, MMU protection/restart
отсутствуют, XM не проверена. [Отчёт](area-fram-state-cp48.md),
[manifest](verification-cp48.json).

## CP47 — shared FRAM RX сэкономил 5 LUT / 8 FF, fit ещё не достигнут

Три кандидата byte-mux/shared-rx/combined прошли 6 positive formal runs,
18 unit/miter tests, две намеренные ошибки обнаружены proof и simulation.
Shared-rx и combined также проходят весь upper FRAM через CPU, vendor/edges/full bus
и cold RT-11FB + DIR с прежними counts/UART. High rdata в shared RX временно
служит shift register; данные валидны на ready и в idle.

Полный synthesis: baseline 1302 LUT, byte-mux 1326, shared-rx 1297,
combined 1317. Лучший CP47c сохранён как экспериментальная основа:
351 FF / 7 EBR / 650 slices, превышение 17 LUT / 10 slices. Все четыре
MAP FAIL, PAR/TRACE/Fmax отсутствуют. Final EDIF: 3504 nets, конфликтующих
направленных драйверов и необъяснённых floating inputs нет; три намеренных
дефекта обнаружены. Это структурный аудит, не проверка routed timing.
В production изменения не приняты; CP45k/production CP40h/APR CP43d/плата
CP29a прежние. MMU protection/restart отсутствуют, XM не проверена.
[Отчёт](area-fram-cp47.md),
[manifest](verification-cp47.json).

## CP46 — control/region альтернативы отклонены

PA17 + RAM/I/O qualifiers, уравнения phase bits, APR RAM address/WE и их
сочетание дали 1315–1317 LUT против 1302 у CP45k. Все gates — MAP FAIL;
Fmax нет, изменений в production нет. Formal/mutations, X/Z data, C oracle,
APR portable/vendor, весь upper FRAM, edge cases и cold FB + DIR проходят
с прежними clocks/UART. Final EDIF audit CP45k/CP46d не нашёл конфликтующих
направленных drivers; INOUT и masked CIN разобраны отдельно.

Лучший relocation prototype остаётся CP45k: 1302 LUT / 359 FF / 7 EBR /
652 slices. Следующий area experiment — SPI FRAM transport byte/state mux.
Protection/restart, active-MMU RK/high DMA и RT-11XM ещё не проверены/не готовы.
[Отчёт](area-control-cp46.md), [manifest](verification-cp46.json).

## CP45 — physical bus уменьшен, fit ещё не пройден

Финальный CP45k: **1302 LUT / 359 FF / 7 EBR / 652 slices**, −49 LUT /
−24 slices от CP44e. Все 11 full-board gates не проходят MAP по ресурсам,
Fmax нет. До границы устройства остаются 22 LUT / 12 slices без резерва
на protection/restart. Рабочие production CP40h/APR CP43d и плата CP29a прежние.

Выбранный `narrow-rom`: общий I/O read qualifier, exact prefix decode и
узкий local bootstrap ROM. Native CPU, MMU bridge, APR storage, firmware,
954 microinstructions, ACK/state/peripherals не менялись. Binary/XZ proof,
CPU/FRAM/vendor/bus и cold FB + DIR прошли; все counts и UART совпали с CP44.
Следующий gate — control/handshake/physical request path; ограничения
MMU и отсутствие RT-11XM result сохраняются.
[Отчёт](area-bus-cp45.md), [manifest](verification-cp45.json).

## CP44 — CPU relocation прототип, area gate не пройден

Kernel unified PAR relocation 18/22 bits, MMR0 software controls, MMR3<4>,
PA22 decode/NXM и доступ CPU ко всему верхнему банку FRAM проверены.
Полные portable CPU sweeps, vendor subset, C oracles, mapped stack/byte/odd/
instruction-stream tests, bus sweep и cold RT-11FB + DIR прошли.
FB использует mapped memory и upper FRAM, UART совпал с CP43.

Лучший из пяти gates — 1351 LUT / 359 FF / 7 EBR / 676 slices, MAP FAIL.
Fmax нет, **CP44 не принят**, native production CP40h, APR CP43d и плата
CP29a неизменны. Microcoded/direct варианты — 970/954 words, +17/+2 clocks
на mapped beat. Следующий gate — снижение общей площади полного board.
PDR protection/W, hardware MMR0 fault metadata, MMR1/2, abort250/restart,
modes/I-D, active-MMU RK test/high DMA и RT-11XM ещё впереди.
[Отчёт](relocation-cp44.md), [manifest](verification-cp44.json).

## CP43 — MMR3 CSR в отдельном APR board

Финальный CP43d: **1258 LUT / 351 FF / 7 EBR / 630 slices / 30.866 MHz**.
Canonical VA172516, six-bit storage, word/byte writes, RESET; CPU portable/
vendor, exhaustive board decode/DMA и 262160 C differential commands прошли.
APR CPU tests и cold RT-11FB + DIR сохраняют прежние counts и UART.
Microcode 963 words, native production
CP40h и физическая плата CP29a прежние. Свободны 22 LUT / 10 slices / 0 EBR.
MMR3 bits пока только хранятся: MMR0/1/2, relocation, modes/I-D/CSM/MAP,
PDR checks/W/abort/restart, CPU PA22, high DMA и RT-11XM ещё не реализованы.
[Отчёт](mmr3-cp43.md), [manifest](verification-cp43.json).

## CP42 — D-input только в APR-сборке

Финальный CP42d: **1248 LUT / 344 FF / 7 EBR / 625 slices / 30.896 MHz**,
−10 LUT от CP40i, MAP/PAR/TRACE PASS при 29.56 MHz. Свободны 32 LUT / 15 slices.
Native production CP40h сохранён: тот же вариант без APR дал +1 LUT.
Рабочий экспериментальный генератор изменяет только D-cone; state, native RTL,
microcode 954/963 words, память и периферия прежние. Новых тактов нет.

SAT/four-state/lint, 69632 CPU cases, CSR portable/vendor, FIS RAM/FRAM/vendor
и cold FB + DIR прошли. Cycle CSV, board counts и UART совпали с CP40.
FPGA CP29a; translation/MMR/high DMA/XM ещё впереди.
[Отчёт](area-d-input-cp42.md), [manifest](verification-cp42.json).

## CP41 — отрицательный результат area-эксперимента

Четыре эквивалентные перестройки микросеквенсора отклонены: A/B/D не прошли
MAP по slices, C занял 1267 LUT и не прошёл 29.56 MHz (TRACE 29.387).
Неизменённый CP40 повторно дал 1258 LUT / 344 FF / 7 EBR / 30.254 MHz с APR.
Новых свободных ресурсов нет; все рабочие RTL, firmware и microcode прежние.
Восемь formal/simulation/lint проверок прошли, три внесённых дефекта обнаружены.
CPU/FIS/FB не перезапускались: их полные CP40 input hashes сохранены.
[Отчёт](area-sequencer-cp41.md), [manifest](verification-cp41.json).

## CP40 — площадь datapath/ALU

Production CP40h: **1159 LUT / 326 FF / 6 EBR / 584 slices / 31.470 MHz**,
−29 LUT от CP38f. APR CP40i: **1258 LUT / 344 FF / 7 EBR / 631 slices /
30.254 MHz**, −10 LUT от CP39d. Оба MAP/PAR/TRACE PASS при 29.56 MHz;
остаток 121/22 LUT и 56/9 slices. Полный MMU fit ещё не доказан.

Изменены только выбор операндов/данных writeback и результата ALU.
Микрокод, state, RF/Q, flags logic и периферия прежние. Formal, four-state,
CPU miter, FIS/RAM/FRAM/vendor и CPU APR CSR tests прошли; оба cold FB runs
сохранили clocks и UART. FPGA CP29a, translation/MMR/high DMA/XM ещё впереди.
[Контракт и все девять fits](area-datapath.md), [manifest](verification-cp40.json).

## CP39 — CPU APR CSR и общий EBR

Experimental CP39d: **1268 LUT / 344 FF / 7 EBR / 635 slices / 31.075 MHz**,
MAP/PAR/TRACE PASS при 29.56 MHz. Все 96 CSR доступны CPU через MOV/MOVB;
lookup использует тот же EBR, byte writes очищают парный W, physical RK DMA
не перехватывается CSR. От CP38g +40 LUT / +3 FF, 963 слова микрокода прежние.
Осталось **12 LUT / 5 slices / 0 EBR**: полный MMU fit ещё не доказан.

Portable/vendor shared-port/CPU/bus tests, 69632-case CPU miter, strict lint,
четыре отрицательных контроля и cold RT-11FB + DIR прошли. FB counts и UART
совпали с CP38g. Production CP38f и плата CP29a сохранены. Translation/MMR,
automatic W, abort250/restart, CPU PA22/high DMA и RT-11XM ещё впереди.
[Контракт и измерения](mmu-apr-csr.md), [manifest](verification-cp39.json).

## CP38 — площадь board read mux

Production теперь **CP38f: 1188 LUT / 326 FF / 6 EBR / 597 slices /
30.943 MHz** (−34 LUT от CP36f). С APR lookup **CP38g: 1228 LUT /
341 FF / 7 EBR / 619 slices / 32.470 MHz** (−37 LUT от CP37e).
Оба MAP/PAR/TRACE PASS при 29.56 MHz. Свободны 92/52 LUT и 43/21 slices.

В рабочем RTL изменён только read mux: firmware/FRAM выбираются отдельно
от малых устройств. Decoder, state/ACK, периферия, CPU и ROM images прежние.
Четыре варианта прошли formal equivalence (38 outputs), два внесённых
дефекта обнаружены. Portable/vendor board bus tests и оба cold RT-11FB
runs сохранили счётчики и UART побайтно. FPGA остаётся CP29a; APR CSR,
translation/MMR/PA22/high DMA и RT-11XM ещё впереди.
[Измерения и контракт](area-board-read.md), [manifest](verification-cp38.json).

## CP37 — чтение PAR/PDR через общую ALU

Experimental CP37d/e: **1265 LUT / 341 FF / 7 EBR / 635 slices / 30.327 MHz**,
MAP/PAR/TRACE PASS при 29.56 MHz. Три первых варианта превысили HC1200:
1296/1300/1286 LUT. Убраны лишние EBR waits, D-input оформлен отдельным
masked OR. Helper 9 words, 963/1024×36, +10 clocks/memory word без hold.
Осталось 15 LUT / 5 slices / 0 EBR — полноценная MMU ещё не помещена.

69632 CPU cases, все 88 memory words/8 APR pages, 781100 PAR/PDR reads;
reset в девяти позициях, six negative controls, FIS на RAM/FRAM/vendor EBR
и cold RT-11FB + DIR прошли. Последний run: 412130048 clocks, UART совпал
с CP36. APR writes/CPU CSR, translation/MMR/PA22/abort/high DMA исключены
из этого read-only gate; XM не загружен. Production CP36f и плата CP29a
сохранены. [Контракт и измерения](mmu-apr-lookup.md), [manifest](verification-cp37.json).

## CP36 — площадь opcode path и word bus

Production теперь **CP36f: 1222 LUT / 326 FF / 6 EBR / 614 slices /
31.116 MHz**, −30 LUT от CP31c. Свободны 58 LUT и 26 slices.
CP36g с experimental context hook: **1243 LUT / 338 FF / 6 EBR /
626 slices / 31.107 MHz**, −30 LUT от CP35e; свободны 37 LUT и 14 slices.
Оба MAP/PAR/TRACE PASS при 29.56 MHz, плата остаётся CP29a.

Изменены только opcode index и положение operand byte-lane mux. Board
передаёт aligned word в CPU, opcode ROM читает его напрямую; engine
получает прежние right-justified byte operands. Default core interface
сохранён параметром ALIGNED_WORD_READS=0. ROM images, ISA, RF/ALU/Q,
engine/sequencer, firmware и peripheral RTL не меняются.

Для двух index-форм прошли все 65536 opcode/hold на portable/vendor EBR.
CPU miter: 69632 cases, все 88 memory words, 1024 дополнительных Icarus cases;
перестановка byte lanes обнаружена. FIS с context hook прошёл на RAM,
SPI FRAM и vendor ROM. Два cold RT-11FB runs сохранили counts и UART:
354938300 clocks без hook, 406268404 с hook. MMU translation/MMR/PA22 CPU bus
и extended RK DMA ещё не подключены; RT-11XM не загружен.
[Измерения и contract](area-decode.md), [manifest](verification-cp36.json).

## CP35 — microcode context entry/return

Проверен служебный вход перед всеми 88 memory words, отдельный saved uPC
и возврат без порчи PSW/MDR/IR/Q, живых temporaries и занятого EA CALL link.
Контроллер — 12 FF; helper — 9 слов, 8 исполняются, +9 clocks/entry без hold.
Экспериментальная сборка 963/1024×36, production остаётся 954/1024×36.

CPU probe 690→732 LUT; masked-OR вариант 814 LUT отвергнут. Полный board
с enable=1 / hold=0: **1273 LUT / 338 FF / 6 EBR / 638 slices / 30.498 MHz**,
MAP/PAR/TRACE PASS при 29.56 MHz. Осталось 7 LUT и 2 slices: до подключения
APR/translation/MMR требуется экономия общей логики.

Прошли 69632 CPU miter cases, все 88 memory words, 390550 entry/return,
1244984 held edges; дополнительно 1024 four-state cases. Пять внесённых
дефектов обнаружены. FIS: 23840 cases на RAM и столько же на SPI FRAM,
645 на vendor DP8KC. Cold RT-11FB + DIR: 406268404 clocks, 3270 UART wire
bytes, 162 SD reads / 6 writes, исходный образ не изменён.

MMU пока не подключён: нет lookup/translation, MMR, PA22 CPU bus,
MMU abort/restart или high-memory RK DMA. RT-11XM не загружен.
Production inputs совпадают с CP31c, FPGA остаётся CP29a.
[Контракт и измерения](mmu-entry.md), [manifest](verification-cp35.json).

## CP34 — разделение ALU и время жизни T5–T7

Три изолированных HC1200 datapath probes прошли MAP/PAR/TRACE:
dedicated arithmetic — 471 LUT / 173 FF / 0 EBR / 42.535 MHz;
full sharing — 487 / 173 / 0 / 39.156 MHz, отвергнут;
relocation-only — 466 / 173 / 0 / 41.315 MHz, только эксперимент.
157 FF относятся к стенду, 16 — Q. В production ничего не перенесено.

Оба кандидата прошли по 16977381 comparison cycles и 265701 four-state
cycles; четыре намеренные ошибки обнаружены. Анализ текущего ROM показывает
T5–T7 свободными на всех 88 memory words. Проверка CPU подменяет эти
регистры во время memory cycles и сохраняет исходные architectural/bus
ожидания; дополнительная порча T0 проверяет чувствительность FIS oracle.
Прошли 21 suite / 272917 cases / 3311955 подмен; покрыты все 88 memory uPC.

Полный board остаётся CP31c: 1252 LUT, 6 EBR, 954 microinstructions,
MMU к CPU не подключён. Следующий эксперимент — MMU entry/return с T5–T7,
сохранением остальных данных и измерением стоимости sequencer/context.
RT-11XM не загружен; FPGA остаётся CP29a. [Подробности](mmu-sharing.md),
[manifest](verification-cp34.json).

## CP33 — EBR PAR/PDR и physical CSR decode

Проверены store PAR16/PDR16, все byte masks, парный W clear, explicit W set,
reserved bits, held/idle/reset. Один EBR, 64 storage pairs, из них 48
доступны через K/S/U I/D CSR. Processor modes и автоматический выбор APR
по PSW пока не реализованы. CP33c probe: **40 LUT / 65 FF / 1 EBR /
32 slices / 96.862 MHz**, PASS; 62 FF измерительные, 3 — контроллер.
1144373 команды на каждом portable/vendor варианте, decode всех 4194304
PA22 bytes и 65536 C cases с serial lookup + translation прошли.

Production остаётся CP31c без MMU, microstore 954/1024×36 v12. MMR0/1/2/3,
W timing при faults, CPU abort/restart и high-memory RK DMA ещё отсутствуют;
RT-11XM не загружен с MMU. Для полного fit нужна экономия LUT, не только
свободный EBR. [Подробности](mmu-apr.md), [manifest](verification-cp33.json).

## CP32 — translator 18/22 bits, PAR16, PA22 и SPI FRAM

Изолированный translator/PDR checker реализован для MMU-off, 18-bit и
22-bit mapping. CP32b: **70 LUT / 80 probe FF / 0 EBR / 41 slices /
93.362 MHz**, MAP/PAR/TRACE PASS. Сам translator комбинационный;
все FF принадлежат измерительному стенду. Прошли 36144800 exhaustive checks,
3114656 four-state checks, 262144 сравнений с существующим C MMU и
196634 запроса через модель SPI FRAM с проверкой всех 128 КиБ.

MMU **пока не подключён к CPU**. PAR/PDR store, MMR CSR, W-bit, abort/restart,
processor modes/SP/I-D и extended RK DMA остаются следующими gates.
MMR3<4> пока подаётся как вход `map22`, а не читается из аппаратного MMR.
Full-board inputs побайтно совпадают с CP31c; новых результатов CPU CPI/Fmax
с MMU нет. RT-11XM из пользовательского образа ещё не загружен на RTL.
[Контракт, измерения и проверки](mmu.md), [manifest](verification-cp32.json).

## CP31 — FIS, FP11 удалён; первый MMU checkpoint

FP11 и ODT отложены. FP RTL/dispatch/state/microcode/build options удалены;
954/1024×36 v12, 70 слов свободно. RK CSR теперь в 16 свободных словах
firmware EBR, верхний банк FRAM больше не занят периферией. Полный HC1200
top CP31c: **1252 LUT / 326 FF / 6 EBR / 628 slices / 30.917 MHz**, PASS.
Cold RT-11FB + DIR, FIS и portable/vendor storage checks прошли.

На этом checkpoint MMU **не подключён к CPU**. Изолированный 18-bit translator/PDR checker
прошёл 1769472 проверки; отсутствуют PAR/PDR storage, MMR registers,
MMU abort/restart и physical DMA extension. Пользовательский образ XM найден:
`../lsi11/disks/rt11v5.3/system.dsk`. Его загрузка и работа >64 КиБ ещё
не проверены. [Измерения, критерии готовности и дальнейшие gates](mmu.md).

Целевой профиль включает **18/22-bit mapping через MMR3<4>**, PAR16 и PA22.
18-bit probe CP31 — промежуточный результат; CP32 выше расширяет его
до 18/22 bits и добавляет отдельные проверки обоих режимов и MMU-off.

CP30 FP control/state сохранён в истории (`d59f19c`), полный FP11 не был
реализован. Плата остаётся CP29.

## CP29 — physical HC1200 bring-up

FLASH verification, real RT-11 boot/DIR and RGB/HDSP operation are confirmed. Keyboard codes and panel ESC are user-confirmed. HG read/write and file readback passed on real hardware at 1 kHz; the test daemon was stopped afterward. [Evidence and limits](board-bringup-cp29.md).

**CP28: полный board integration baseline синтезирован, RT-11/DIR прошли в RTL simulation.**
1217 LUT /318 FF /6 EBR /610 slices, 29.56 MHz PASS, TRACE 31.186 MHz,
полный MAP/PAR. Microcode 954/1024×36 v12, 349 labels, без изменений относительно CP27.

FRAM, UART/timer/panel/SD/RK, firmware ROM, reset и физический top включены.
ROM dispatch добавляет один внутренний FETCH clock; 65536 opcode проверены
с portable/vendor ROM. ALU эквивалентен CP27 по SAT и четырёхзначной симуляции.
KW11 timebase имеет прежний точный период при меньшей площади.

RT-11 cold boot + DIR: 355132188 clocks, 3270 UART wire bytes, 98 файлов,
162 SD reads/6 writes; backing image read-only. Прошли scoped integer/fault/FIS,
FRAM и периферийные проверки. [Подробный gate](hc1200-integration.md),
[manifest](verification-cp28.json).

**Ограничения:** всего 63 LUT/30 slices/1 EBR запаса, цель 900–1100 LUT не достигнута.
В общем top нет prefetch; физического программирования, external pin timing,
vendor whole-board RT-11, полного FP11 и register banking нет. MMU отсутствует.
Далее — площадь/prefetch, disk-error/file-write/Ctrl-C coverage, pin timing и плата.

## Исторический CP27

**CP27 завершён: FIS реализован и прошёл portable/vendor проверки.**
FADD/FSUB/FMUL/FDIV: **954/1024×36 v12, 349 labels**. 223 FIS words +31
linking JUMP, все 700 прежних words/271 label сохранены. D/Q расширяет
pair5; RF/Q/register state, sequencer и FRAM transport не расширены.

Core **863 LUT/299 FF/4 EBR**, 35 MHz PASS/TRACE **35.954 MHz**;
FRAM/prefetch/IRQ+probe **1095/416/4**, 29.56 MHz PASS/TRACE **31.300 MHz**.
−4 LUT к обоим CP26 gates, 0 FF/EBR. Полный board top не включён.

23840 FIS expected states проверяют exact F-format arithmetic, все восемь R,
NZVC, trace/IRQ, failed READ/WRITE, odd addresses и odd-SP second faults.
Прошли full portable/vendor RAM/FRAM; все четыре результата совпали побайтно.
24 FIS benchmarks/ROM уже совпали побайтно. 15 мутаций отвергнуты;
datapath legacy-context equivalence доказана с отрицательным carry control.
Свежая integer portable-регрессия: 268843 normal +49596 faults на RAM/FRAM.
Старые vendor integer/1146 benchmarks —исторический CP26, не новый прогон.

Следующий этап — [общий HC1200 top, bootstrap, RK service и RT-11](hc1200-integration.md).
Свободны 70 слов/3 EBR и 185 LUT до физического limit текущего probe scope.
Полный FP11 не реализован и его fit не доказан. Banking/native ODT,
stack-limit recovery и I/O timeout ещё предстоят. MMU отсутствует;
FPGA не программировалась. [FIS и источники](fis.md),
[manifest](verification-cp27.json), [измерения](benchmarks-cp27.json).

## Исторический CP26

**CP26 завершён: DIV во всех восьми S modes**, signed quotient/remainder,
zero/overflow flags и operand fault handling. 58 новых слов/16 меток;
**700/1024×36 v11, 271 labels**, все 642 слова/255 меток CP25 сохранены.
DEC требует even R; поведение odd R — явно описанное SIMH-style расширение.
RF/ALU/Q/sequencer/PSW/FRAM RTL не меняются.

Core **867 LUT/299 FF/4 EBR**, 35 MHz PASS/TRACE **37.151 MHz**;
FRAM/prefetch/IRQ+probe **1099/416/4**, 29.56 MHz PASS/TRACE **31.771 MHz**.
+26/+10 LUT к CP25, без новых FF/EBR. До 1100 остался 1 LUT;
полный board top и external pin timing не включены.

Прошли **268907 instruction cases + 49596 fault frames** на каждом
RAM/FRAM × portable/vendor сочетании, **1146 benchmarks/ROM**. Все 128
result files совпали; все 24 старых C fixtures, 48 cycle CSV и 72 benchmark
JSON побайтно сохранены относительно CP25. 106 synthesis archives и
419 raw report hashes проверены; текущие inputs совпадают с CP26a/b.
[DIV и исправления эталона](eis-div.md), [manifest](verification-cp26.json),
[измерения](benchmarks-cp26.json).

Следующий этап — снижение площади до интеграции полного board top.
Banking, native ODT, stack-limit recovery, I/O timeout и запуск ОС ещё
предстоят. 50 MHz и 900–1000 LUT не достигнуты. MMU отсутствует;
FPGA не программировалась.

## Исторический CP25

**CP25 завершён:** signed MUL, все восемь S addressing modes, odd-register
low-word writeback, full-product NZVC, late R read и operand fault handling.
45 новых слов/10 меток; **642×36 v11, 255 labels**. Все 597 слов/245 меток
CP24 сохранены; RF/ALU/Q/sequencer/PSW/FRAM RTL не меняются.

Core **841 LUT/299 FF/4 EBR**, 35 MHz PASS/TRACE **36.926 MHz**;
FRAM/prefetch/IRQ+probe **1089/416/4**, 29.56 MHz PASS/TRACE **30.672 MHz**.
До 1100 остаётся 11 LUT. Полный board top и external pin timing не включены.

Полная свежая регрессия прошла: **255243 instruction cases и 45596 fault frames**
на каждом RAM/FRAM × portable/vendor сочетании, **1082 benchmarks/ROM**.
Все **120 result files** совпали между ROM models; все **22 прежних C fixtures,
44 cycle CSV и 68 benchmark JSON** побайтно равны CP24, включая такты,
memory beats, SPI transactions и SPI clocks. Из illegal candidate list удалены
512 MUL encodings, прежние 4160 completed illegal fixtures сохранены.
Проверены 104 synthesis archives и 411 raw report hashes; текущие inputs
совпадают с CP25a/b. Прошли C regression, unit/directed tests, lint и lsi11
peripheral checks. Decoder miter проверил все 65536 encodings: отличаются
ровно 512 MUL encodings. [Manifest](verification-cp25.json),
[счётчики и производные метрики](benchmarks-cp25.json).

Две ошибки существующего DCJ11 C MUL исправлены узким patch; отрицательный
контроль воспроизводит их на старом CP24 archive. Новый MUL проверен на
13110 normal/4000 fault cases, 7002 независимых records, 12 negative controls
и 1024 CSR cases/ROM. 861968 arithmetic pairs дополнительно проверены Python.

Следующий EIS этап — DIV. Banking, native ODT, stack-limit recovery, I/O
timeout, полный board top и запуск ОС ещё предстоят. 50 MHz и 900–1000 LUT
для FRAM scope не достигнуты. MMU отсутствует, FPGA не программировалась.
[Полный MUL checkpoint](eis-mul.md).

## Исторический CP24

**CP24 завершён:** XOR с поздним чтением source register по правилам J-11.
Все восемь destination modes используют прежний EA microcode; register mode
занимает один execution cycle. Добавлены пять слов и три метки: **597×36 v11**.
Все 592 прежних слова и 242 метки сохранены, C executor не изменён.

Core **854 LUT/299 FF/4 EBR**, 35 MHz PASS/TRACE **36.059 MHz**;
FRAM/prefetch/IRQ+probe **1089/416/4**, 29.56 MHz PASS/TRACE **30.273 MHz**.
Это +4/+2 LUT к CP23, без новых FF/EBR. До желательных 1100 осталось 11 LUT.
Внешние pin delays и полный UART/timer/panel/SD/RK в fit не включены.

Свежие **242133 instruction cases и 41596 fault frames** прошли на каждом
RAM/FRAM × portable/vendor сочетании; **1018 benchmarks/ROM**. Все 112
result files совпадают между ROM models; все 104 прежних result files и
20 C fixtures побайтно равны CP23. Дополнительно проверены 4698 независимых
XOR records, 385 alias cases, восемь negative controls и 1024 CSR cases/ROM.
Проверены 102 synthesis archives и 403 raw report hashes.

MUL/DIV, banking, native ODT, stack-limit recovery, I/O timeout и полный
board top ещё предстоят. 50 MHz и 900–1000 LUT не достигнуты. MMU отсутствует;
FPGA не программировалась. [XOR gate](eis-xor.md),
[manifest](verification-cp24.json), [benchmarks](benchmarks-cp24.json).

## Исторический CP23

**CP23 завершён:** общий target и low-bit OR-dispatch микросеквенсора.
Core **850 LUT/299 FF/4 EBR**, 35 MHz PASS/TRACE 36.302 MHz;
FRAM/prefetch/IRQ+probe **1087/416/4**, 29.56 MHz PASS/TRACE 30.193 MHz.
Это −6/−11 LUT к CP22, без новых FF/EBR. До желательных 1100 осталось 13 LUT.
Микрокод 592×36 v11, остальные RTL modules, C emulator и ISA сохранены.

74 formal equivalence points proven; неверный repair vector отвергнут.
Свежие 231105 instruction cases и 35836 fault frames прошли на каждом
RAM/FRAM × portable/vendor сочетании; 986 benchmarks/ROM. Все 104 result
files, 20 C fixtures и все microclock/memory/SPI counts побайтно равны CP22.
Проверены 100 synthesis archives и 395 raw report hashes.

Меньшая площадь выбрана по FRAM fit из трёх вариантов. Fmax немного ниже CP22;
29.56 MHz проходит с margin 0.709 ns. Полный board top, banking, native ODT,
MUL/DIV/XOR и остальные прежние ограничения сохраняются. 50 MHz и 900–1000 LUT
ещё не достигнуты. MMU отсутствует; FPGA не программировалась.
[Area gate](area-sequencer.md), [manifest](verification-cp23.json),
[benchmarks](benchmarks-cp23.json).

## Исторический CP22

**CP22 завершён:** полная portable/vendor регрессия прошла. ASHC через прежние ALU/Q;
исправлены ASH/ASHC destination capture после count EA и ASHC N/Z от 32-bit
результата до alias stores. Ошибки исправлены и в нашем DCJ11 C executor;
[patch](cp22-core-fix.patch). MMU отсутствует.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP22c core+probe | 856 | 299 | 4 | 35 MHz PASS, TRACE 37.258 MHz |
| CP22d FRAM/prefetch/IRQ+probe | 1098 | 416 | 4 | 29.56 MHz PASS, TRACE 30.457 MHz |

592 words v11 (+41), 549 прежних words и 235 labels сохранены;
019/01a исправлены. 21 negative control run (13 ASHC/alias + 8 ASH),
17408 независимых проверок 32-bit результата, 14 ASH alias checks и 256 ASHC sign-boundary cases без расхождений.
Существующая общая C core regression прошла. Полная проверка: 231105 instruction
cases и 35836 fault frames на каждом RAM/FRAM × portable/vendor сочетании;
256 alias cases проверены дополнительно. Все 986 benchmarks/ROM прошли,
871 прежний benchmark сохранён; 19 ASH/prefetch workloads изменили timing. Между portable и vendor совпадают 104 основных result files.
Дополнительно сверены две пары alias cycle CSV: по одной для RAM и FRAM.
Проверены 94 synthesis archives и 371 raw report hashes.
[Manifest](verification-cp22.json), [benchmarks](benchmarks-cp22.json).

CP22a/b отклонены по семантике, хотя timing проходил. Их отчёты сохранены
как история. До желательных 1100 осталось 2 LUT: перед MUL/DIV/XOR нужен
area gate. Native ODT, banking, stack-limit recovery, I/O timeout и полный
board top ещё предстоят; 50 MHz не достигнуты. FPGA не программировалась.
[ASHC и исправление oracle](eis-ashc.md).

## Исторический CP21; ASH alias ordering исправлен в CP22

**CP21 выполнен:** ASH во всех8 addressing modes, serial shifts через прежние
RF/ALU, без нового аппаратного состояния. Один kernel register set,
CM=PM=RS=0, NZVC/IPL/T;16-bit addresses, строго без MMU.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP21a core + probe | 849 | 299 | 4 | 35 MHz PASS, TRACE36.876 MHz |
| CP21b FRAM/prefetch/IRQ + probe | 1094 | 416 | 4 | 29.56 MHz PASS, TRACE31.309 MHz |

551/1024 words v11 (+30), все521 прежних words/labels сохранены.
Единственное функциональное изменение RTL — ASH opcode predecode.
202597 completed DCJ11 instruction cases на каждом RAM/FRAM×portable/vendor
сочетании;34812 fault frames. Новая группа:26316 completed/27204 candidates,
888 явных exclusions; отдельно1024 ASH fault frames без exclusions.
Все176281 прежних cases,33788 fault frames и794 benchmark counts сохранены.
890 benchmarks/ROM,96 byte-identical result files,90 synthesis archives/355 raw hashes.

ASH с ideal FETCH: count0 —10; left n —16+4n; right n —13+3n clocks.
FRAM register loops для0/+1/−1 —40.15625 CPI смеси31 ASH+BR.
До желательных1100 осталось6 LUT. Следующий gate должен контролировать
площадь до расширения EIS. MUL/DIV/ASHC/XOR, banking, native ODT,
stack-limit recovery, I/O timeout и полный board top ещё не реализованы.
50 MHz остаётся целью; FPGA не программировалась.
[ASH](eis-ash.md), [manifest](verification-cp21.json), [benchmarks](benchmarks-cp21.json).

## Исторический checkpoint CP20


**CP20 выполнен:** HALT restart-профиль существующего DCJ11 emulator и
синхронный peripheral RESET. Полный native console ODT отсутствует.
Один kernel register set, NZVC/IPL/T; 16-bit addresses, без MMU.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP20c core + probe | 836 | 299 | 4 | 35 MHz PASS, TRACE 36.647 MHz |
| CP20d FRAM/prefetch/IRQ + probe | 1085 | 416 | 4 | 29.56 MHz PASS, TRACE 30.593 MHz |

521 words v11 (+14), все 507 прежних words/labels сохранены. Новое поле
JUMP.init — зарегистрированный выход, пригодный для async reset UART.
FF+2 к CP19: регистр импульса и observation FF в probe.
176281 completed DCJ11 cases на каждом RAM/FRAM×portable/vendor сочетании,
33788 прежних fault frames, 64 HALT fault checks, 3 actual-peripheral scenarios.
Новые 6144 cases без exclusions; прежние exclusions остаются документированными.
794 benchmarks/ROM, все 786 прежних counts сохранены.88 portable/vendor result
files:32 cycle CSV +56 benchmark JSON.88 synthesis archives/347 raw hashes.

RESET сохраняет RF/PSW и CPU FRAM, очищает pending peripheral IRQ; новый
KW11 tick после RESET снова будит WAIT. HALT internal fault использует terminal
double-fault policy; abort parity с C для этих случаев не заявлена.
Следующий отдельный gate — EIS. До желательных 1100 LUT осталось 15; полный
board top, banking, native ODT, stack-limit recovery и I/O timeout ещё впереди.
[HALT/RESET](system-control.md), [manifest](verification-cp20.json),
[benchmarks](benchmarks-cp20.json).

## Исторический checkpoint CP19

**CP19 выполнен:** MFPS/MTPS во всех восьми addressing modes. Один kernel
register set, CM=PM=RS=0, NZVC/IPL/T,16-bit addresses, без MMU.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP19a core + probe | 844 | 297 | 4 | 35 MHz PASS, TRACE 36.426 MHz |
| CP19b FRAM/prefetch/IRQ resolver + probe | 1073 | 414 | 4 | 29.56 MHz PASS, TRACE 30.046 MHz |

507 words v10, +14; сохранены все 493 прежних words/labels и весь RTL кроме
opcode decoder. FF/EBR обоих scopes неизменны; относительно CP18 +1/+11 LUT.
170137 completed DCJ11 cases (157285 прежних +12852 новых), отдельно 33788
fault frames (32780 прежних +1008 новых) на каждой RAM/FRAM×portable/vendor ROM.
204 новых normal/trace/IRQ candidates явно исключены по actual abort/I/O/stack
status, без заявления совместимости этих случаев. Новая fault группа без exclusions.
786 benchmarks/ROM; все 766 прежних counts,13 fixtures и 26 cycle CSV сохранены.
Всего 84 synthesis archives/331 raw report hashes проверены.82 portable/vendor
result files совпали побайтно:30 cycle CSV и 52 benchmark JSON.

MFPS Rn — 2 clocks с ideal FETCH, MTPS Rn — 8. FRAM/prefetch скрывает разницу
в новых register loops (40.15625 CPI). MTPS immediate пока использует общий
byte data READ; его отдельный stream fast path не добавлялся.

Следующие gates — HALT/RESET и нужные system operations, затем EIS. Banking,
stack-limit recovery, I/O timeout и полный board top ещё не реализованы.
50 MHz остаётся целью; запас 207 LUT относится только к измеренному FRAM probe.
[MFPS/MTPS](psw-transfer.md), [manifest](verification-cp19.json), [benchmarks](benchmarks-cp19.json).

## Исторический checkpoint CP18

**CP18 выполнен:** все32 CC/NOP encodings и MFPT. Один kernel register set,
CM=PM=RS=0, NZVC/IPL/T, 16-bit addresses, без MMU.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP18e core + probe | 843 | 297 | 4 | 35 MHz PASS, TRACE35.674 MHz |
| CP18f FRAM/prefetch/IRQ resolver + probe | 1062 | 414 | 4 | 29.56 MHz PASS, TRACE30.409 MHz |

493 words v10; прежние452 words/labels и весь RTL кроме decoder сохранены.
157285 completed DCJ11 cases (136165 CP17 +21120 новых, без новых exclusions),
отдельно32780 fault-frame cases на каждой RAM/FRAM×portable/vendor ROM.
766 benchmarks/ROM, все746 прежних counts неизменны. Все три fit-варианта
сохранены; CP18d отклонён по FRAM area1134 LUT.82 archives/323 raw hashes.

Следующие gates — MFPS/MTPS, HALT/RESET, затем EIS. Banking, stack-limit
recovery, I/O timeout и полный board top ещё не реализованы.
[CC/NOP/MFPT](system-flags.md), [manifest](verification-cp18.json).

## Исторический checkpoint CP17

**CP17 выполнен:** trace и RTT, один kernel register set, CM=PM=RS=0,
NZVC/IPL/T, 16-bit addresses, без MMU.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP17a core + probe | 826 | 297 | 4 | 35 MHz PASS, TRACE 36.302 MHz |
| CP17b FRAM/prefetch/IRQ resolver + probe | 1046 | 414 | 4 | 29.56 MHz PASS, TRACE 30.194 MHz |

452 words v10, +1 state FF; сохранены RF/Q/ALU и FRAM transport.
136165 completed DCJ11 cases (124969 CP16 + 11196 trace/RTT), отдельно
32780 CP16 fault-frame cases на каждой RAM/FRAM × portable/vendor ROM.
24 новых FRAM-system cases и 140 trace/fault cases. 746 benchmarks/ROM,
все 726 прежних counts неизменны. 76 synthesis archives/299 raw hashes.

Ещё предстоят HALT/RESET, остальные PSW/system operations, EIS, banking,
red/yellow stack limits, I/O timeout и полный board top. 50 MHz пока не достигнуты.
[Trace/RTT и ограничения](trace-rtt.md), [manifest](verification-cp17.json).

## Исторический checkpoint CP16

**CP16 выполнен:** memory bus/address errors вызывают vector004, fault autoincrement
восстанавливается без штрафа успешной инструкции, ошибка внутри frame — terminal STOP.
Сохранены основной integer subset Stage 1, все 8 addressing modes, byte/word,
IRQ/WAIT/SPL, KW11/KL11 resolver, software/reserved traps, RTI и SPI FRAM prefetch.
Один kernel register set, CM=PM=RS=T=0. MMU отсутствует полностью.

| Scope | LUT4 | FF | EBR | Timing |
|---|---:|---:|---:|---|
| CP16f core + FRAM/prefetch + IRQ adapter + probe | **1042** | **413** | **4** | **29.56 MHz PASS**, Fmax 31.117 MHz |
| CP16e core + generic IRQ + probe | **809** | **296** | **4** | **35 MHz PASS**, Fmax 36.552 MHz |

452 words, encoding v9; RF16×16, Q16, один 16-bit ALU, 1024×36 microstore.
По сравнению с CP15: три state FF, два ROM words, десять изменённых words;
labels и успешные instruction counts сохранены. UART/timer/panel/SD/RK board
fit и внешние pin delays ещё не измерены; FPGA не программировалась.

**Проверено:** 124969 completed DCJ11 instruction cases и 32780 fault-frame
cases на RAM/FRAM × portable/vendor ROM; 32 FRAM-system scenarios и 96 новых
second-fault checks. Все 726 прежних benchmarks и все десять cycle CSV совпали
с CP15. 14 Python methods, 2097152 byte ALU checks, 16384 byte RF checks.
74 synthesis archives/291 raw report hashes проверены; current inputs совпадают с CP16e/f.

**Ещё предстоят:** trace/RTT, HALT/RESET и остальные PSW/system operations,
EIS, J-11 bank/mode exchange, red/yellow stack limits, I/O timeout и полный board top.
Отсутствующая ISA пока получает reserved vector010; это не её реализация.

[Memory fault profile и oracle limits](memory-faults.md), [manifest](verification-cp16.json),
[benchmarks](benchmarks-cp16.json), [synthesis](synthesis.md).

## Исторические проверки CP15

124969 completed DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
все 120809 CP14 и 4160 reserved/invalid-mode cases. Из 18536 новых кандидатов
14376 с другим поведением reference исключены явно; они не считаются
совместимыми инструкциями. 96 новых directed frame cases. Все 446 CP14 words,
labels, девять fixtures и per-case cycles неизменны.

726 benchmark runs на ROM-модель; все 714 прежних counts сохранены.
Exhaustive RTL miter подтвердил эквивалентность decoder финального варианта
и базового CP15a для всех 65536 opcodes. Проверены 68 source archives,
267 raw report hashes; current fit inputs совпадают с CP15e/f.

[Semantics и exclusions](reserved-traps.md), [manifest](verification-cp15.json),
[benchmarks](benchmarks-cp15.json), [portable](../tb/reports/cp15-tests.log),
[vendor ISA](../tb/reports/cp15-vendor-isa.log).

## Исторические проверки CP14

120809 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
все 114155 CP13 и 6654 новых IRQ/WAIT/SPL cases без exclusions.
104 directed IRQ cases, 763 adapter checks, пять actual legacy peripheral
scenarios с шестью проверками установившегося состояния. 13 Python methods;
56268 accepted encodings из 65536. Все 422 CP13 words/labels, восемь fixtures
и per-case cycle CSV неизменны. Reset expectation намеренно исправлено на IPL7.

714 benchmark runs на ROM-модель; все 690 прежних counts сохранены.
Nested IRQ→IRQ→RTI→RTI возвращает стек и PSW. Проверены 62 source archives
и 243 raw report hashes; final fit inputs совпадают с CP14c/d.

[IRQ semantics](interrupts.md), [manifest](verification-cp14.json),
[benchmarks](benchmarks-cp14.json), [portable](../tb/reports/cp14-tests.log),
[vendor ISA](../tb/reports/cp14-vendor-isa.log).

## Исторические проверки CP13

114155 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
все 109515 CP12 и 4640 новых trap/RTI cases без exclusions.
112 directed cases проверяют ACK errors и odd SP. Все 65536 encodings
проверены, 56259 поддержаны. Все 393 CP12 words, labels, семь старых
fixtures и per-case cycles неизменны.

690 benchmark runs на ROM-модель, все 666 прежних counts сохранены.
Проверены 58 source archives и 227 raw report hashes; текущие fit inputs
совпадают с CP13a/b. Исправлена сериализация oracle fixture при наложении
stack patches на opcode: исходный emulator не изменён.

[Trap profile и ограничения](software-traps.md), [manifest](verification-cp13.json),
[benchmarks](benchmarks-cp13.json), [portable](../tb/reports/cp13-tests.log),
[vendor ISA](../tb/reports/cp13-vendor-isa.log).

## Исторические проверки CP12

109515 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
все 104991 CP11 и 4524 SWAB/SXT/MARK. 36 новых directed cases, 55744
accepted decoder encodings из 65536 проверенных. Все 355 прежних words,
label addresses, fixtures и cycle CSV остались неизменными.

666 benchmark runs на ROM-модель, все 638 прежних counts сохранены.
Portable/vendor результаты совпадают. Проверены 56 source archives и
219 raw report hashes; текущие synthesis inputs совпадают с CP12c/d.
Новый priority-decoder fit 1081 LUT отвергнут по area, parallel masks дали
984 LUT. Оба варианта и все исходные отчёты сохранены.

[Microcode, tests и exclusions](extra-instructions.md),
[manifest](verification-cp12.json), [benchmarks](benchmarks-cp12.json),
[portable](../tb/reports/cp12-tests.log), [vendor ISA](../tb/reports/cp12-vendor-isa.log).

## Исторические проверки CP11

104991 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
все 90177 CP10 и 14814 новых JMP/JSR/RTS/SOB. 58 directed control tests;
65536 decoder encodings (55552 accepted), 12 Python methods и 4324
sequencer checks. Все прежние primitives, byte/word flags и peripheral tests проходят.

638 benchmark runs на ROM-модель: все 606 CP10 без изменения counts и
32 новых control/stack/program runs. Portable/vendor JSON и per-case CSV
совпадают. На момент CP11 recorder проверил 52 source archives, 203 raw report hashes
и совпадение fit inputs с CP11e/f. Документы и исходные reports
сохраняются отдельно для каждого checkpoint.

Control oracle дополнительно проверяет сам факт vector entry: yellow-stack
trap может очистить fTrap внутри core_step. 456 из 15270 кандидатов исключены
явно (448 trap, 402 I/O, overlap 394). Исходный emulator не изменён.
[Control ISA](control-flow.md), [manifest](verification-cp11.json),
[benchmarks](benchmarks-cp11.json), [portable](../tb/reports/cp11-tests.log),
[vendor ISA](../tb/reports/cp11-vendor-isa.log).

## Исторические проверки CP10

90177 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
12928 RR/BR + 31671 word EA + 7080 word unary + 7440 branch + 23954 byte EA
+ 7104 byte unary. Старые fixtures и per-case cycle CSV не изменились.
Byte ALU: 2097152 checks; writeback: 16384; directed byte CSR/faults: 23+46.
525 prefetch beats, 19 legacy peripheral beats, 65536 decoder encodings
(54528 accepted), 11 Python methods и все прежние primitive tests проходят.

606 benchmark runs на каждую ROM-модель, в том числе все 430 CP9 без
изменения clocks/bus/SPI counts. Portable/vendor результаты совпадают.
На момент CP10 recorder проверил 46 source archives, 179 raw report hashes
и совпадение RTL/microcode/assembler/ROM с final CP10j/k synthesis inputs.

[Byte ISA и exclusions](byte-instructions.md), [manifest](verification-cp10.json),
[benchmarks](benchmarks-cp10.json), [portable log](../tb/reports/cp10-tests.log),
[vendor ISA](../tb/reports/cp10-vendor-isa.log). Исходный DCJ11 emulator не
изменён; четыре read-only address hooks добавлены только в build copy,
чтобы явно исключать внутренние CSR, обходящие public bus callbacks.

## Исторические проверки CP9

* 59119 завершённых DCJ11 cases на каждую RAM/FRAM × portable/vendor ROM:
  12928 RR/BR + 31671 EA + 7080 unary + 7440 branch. Все прежние CP8
  fixtures и EA per-case cycles сохранены без изменений.
* Unary: 7104 кандидата; 24 abort явно исключены. Все 12×8 modes, operand
  edges с 16 NZVC combinations, PC/SP и indexed wrap. 19588 exact bus beats;
  99242 RAM clocks с 0..3 waits, 2035650 FRAM clocks.
* Branch: все 15×16 class/flags combinations и 256 offsets у каждого class;
  7440 cases без exclusions, 7440 bus beats; RAM 35464 clocks,
  FRAM 805504. Полный Cartesian product offsets/flags не заявлен.
* 46 unary directed cases проверяют CSR reads/writes, read/write errors,
  odd-word rejection и request stability с 0..3 waits. CLR не читает конечный
  destination; TST не пишет; PSW фиксируется после успешного WRITE ACK.
* Все 65536 decoder encodings проверены; **33280 поддержанных**. Прежние
  10 Python, 66592 ALU, 4096 pairs, 4323 sequencer checks, 503 prefetch
  beats и 19 actual legacy peripheral beats проходят.
* **430 benchmark runs на ROM-модель:** 154 прежних CP8 без изменения counts
  и 276 новых (69 workloads × 4 memory modes). Все portable/vendor JSON
  и differential cycle CSV должны совпадать побайтно.
* Speculation на conditional branch приостановлена микрокодом после
  выявленных лишних SPI reads. Сохранены unrestricted policy measurements.
  BNE self-loop: 144→108 CPI; countdown: 88.176471→72.352941 CPI.

[Verification manifest](verification-cp9.json), [benchmarks](benchmarks-cp9.json),
[portable log](../tb/reports/cp9-tests.log), [vendor ISA](../tb/reports/cp9-vendor-isa.log).
Compressed fixtures/cycle CSV: `tb/reports/cp9-*.gz`. На момент завершения CP9 RTL/microcode/assembler/ROM hashes совпадали
с его финальными CP9e/f synthesis inputs.
Общие oracle callbacks вынесены в `tb/trace_oracle.h`; исправлена строковая
выборка suite name в общем testbench. Это изменения verification, не core.

## Исторические проверки CP8

* 10 Python test methods; v4 BA encoding, запрет старого DZ, packing/roundtrip,
  controls, stream qualifiers и conflicts. Generated ROM не изменился после
  исправления только banner/docstring assembler с v3 на v4; hashes зафиксированы.
* 66592 независимых ALU result/NZVC checks; RF16, 256 dual reads,
  4096 operand-pair checks, BA SUB/BIC writeback, Q/shift/stall/reset.
* 4323 sequencer checks, 1024 ROM reads/holds и 17 feedback transitions;
  memory/PSW masks, byte lanes и READ/WRITE/MDR с 0..7 waits.
* Все 65536 opcodes: **28928 accepted encodings**. Dynamic stream hint:
  65536 encodings; prefetch policy/stalls/reset и 503 transport beats.
* **12928 RR/BR DCJ11 cases**, включая исходные 6272 без изменения порядка.
  RAM с 0..3 waits: **45248 clocks**, FRAM: **1383296 clocks**.
* **31671 EA DCJ11 cases**, все **7×8×8 mode pairs**, registers/PSW и точные
  **137006 bus beats**. RAM 0..3 waits: **948247 clocks**; FRAM:
  **14739672 clocks**. Из 32298 кандидатов исключены 627: 595 abort и 32 I/O.
* 14 directed cases: семь I/O operations, odd source/pointer, пять store errors
  без фиксации нового PSW или записи в memory. CMP/BIT не пишут destination;
  MOV не читает конечный destination; BIC/BIS/ADD/SUB делают один read + write.
* Integration с настоящей замороженной периферией lsi11-fpga: 19 beats —
  KL11, KW11, panel, SD side effects и неизвестный CSR без FRAM alias.
* RR/EA и memory microprograms проверены с portable ROM и vendor DP8KC;
  per-case cycle CSV совпадают побайтно. Verilator --Wall без предупреждений.
* **154 benchmark runs на каждую ROM-модель:** 27 RR RAM, 27 RR FRAM,
  100 EA (25 workloads × 4 memory modes). Все counts совпадают; все прежние
  CP7 benchmarks дали прежнее число clocks, memory beats и SPI transfers.

Полный regression: `make test`. Vendor: `make vendor-test vendor-engine
vendor-memory-engine vendor-core vendor-fram vendor-ea`, с корректным
`LATTICE_SIM_DIR`. Увеличенный RR suite превысил старый общий timeout testbench;
timeout теперь зависит от числа fixtures. CPU/FRAM протокол для этого не менялся.

[Verification manifest](verification-cp8.json), [benchmarks](benchmarks-cp8.json),
[tests](../tb/reports/cp8-tests.log), [vendor EA](../tb/reports/cp8-vendor-ea.log).
Compressed oracle fixtures и cycle CSV сохранены в `tb/reports/cp8-*.gz`.
Архивы и реальные timing failures сохранены, а не перезаписаны проходящим run.

При abort частичное состояние регистров не заявляется идентичным DCJ11:
source mode2/3 increment выполняется после READ ACK. Architectural trap
frame/vector fetch и restart относятся к Stage 2. Byte ISA появилась позднее, в CP10. Подробности: [word-double-operand.md](word-double-operand.md).

## Следующий gate

Architectural memory bus/address traps с очисткой EA CALL state и terminal
frame-fault guard; затем trace/RTT, HALT и прочие system instructions.
EIS идёт отдельным измеренным gate. У CP15f probe остаются 244 LUT и 3 EBR;
полный core с периферией ещё не синтезирован вместе. Эти ресурсы не
резервируются под MMU. [IRQ и ограничения](interrupts.md).
