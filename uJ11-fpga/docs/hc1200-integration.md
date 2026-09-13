# CP28: полный HC1200 top, FRAM и RT-11

> Исторический integration baseline. Текущие два банка FRAM, ROM boot,
> SD/RK/CSR и ресурсы описаны в [system-cp67](system-cp67.md),
> сборка без microasm11 — в [development-cp67](development-cp67.md).

This is the CP28 integration baseline. [CP29 physical bring-up](board-bringup-cp29.md)
adds synchronized panel/HG inputs and records the successful FPGA programming,
RT-11 boot, panel/keyboard and HG read/write hardware tests.

Дата: 2026-09-10. **Холодная загрузка RT-11 и DIR прошли в RTL simulation;
полный физический top прошёл Diamond MAP/PAR/TRACE.** Итоговый
[CP28m](../synth/reports/cp28m/result.json): **1217 LUT4 / 318 FF / 6 EBR /
610 slices**, constraint 29.56 MHz PASS, TRACE **31.186 MHz**, все связи разведены.
Это измеренный integration baseline, а не готовность к программированию платы.
Осталось **63 LUT, 30 slices и 1 EBR**; желательные 900–1100 LUT не достигнуты.

В top входят core с integer/EIS/FIS, один контроллер MR45V100A SPI FRAM,
KL11 115200 8N1, KW11-L 50 Hz, panel GPIO, SD SPI byte service, RK611 software
service, bootstrap ROM, OSCH/reset и настоящий SG32 pinout из lsi11-fpga.
**Prefetch в полном top пока выключен.** Прежний FRAM/prefetch core остаётся
в проекте отдельно. MMU, address translation и расширение CPU address отсутствуют.

## Память и firmware

Core сохраняет 16-bit physical address и right-justified byte data. Board
adapter переводит odd-byte data в AM4 byte lanes, а после каждого принятого
beat даёт периферии один sampled request-low clock. Незаполненный CSR не
проваливается в RAM: после насыщения 11-bit timeout counter возвращается bus
error. Медленная SD byte transfer занимает 1088 SCK half-period clocks и
укладывается в этот timeout.

`uj11_board_fram.v` использует READ=03, WREN=06, WRITE=02, SPI mode 0 и
little-endian data. Общая последовательность command/address/data заменяет
дублированные read/write states. Core удерживает запрос, адрес и данные до
ACK, поэтому второй набор address/write-data FF внутри transport удалён.
Контракт удержания обязателен; это board transport, а не drop-request FIFO.

24-bit SPI header нужен самому FRAM. Его старший байт содержит только private
RK storage bank bit; обычный CPU остаётся 16-bit и использует bank 0.
Значение bank 1 хранит RK register subset. Firmware DMA operand может обращаться
к физическому FRAM в диапазоне I/O page; обычная CPU инструкция такого bypass
не получает. Проверка перехвата DMA записи по адресу UART входит в unit gate.
Параметры протокола и источник LAPIS FEDR45V100A-01 приведены в
[предыдущем FRAM audit](fram-peripherals.md).

Существующий `../microasm11` собирает читаемые `firmware/sd_boot.asm` и
`firmware/rk_service.asm`: **426 и 320 bytes**. `build_firmware.py` формирует
listing, бинарники, hashes и один **512×16 logical firmware ROM**. Два порта
одного DP8KC читают младший и старший байты из физических 1024×9; bootstrap
занимает word addresses 0..255, RK service —256..511. Это отдельный firmware
ROM: 139 свободных firmware words не являются свободными microinstructions.

После обычного reset CPU начинает с PC=0. Два overlay words `JMP @#004000`
передают управление SD bootstrap. Он инициализирует SD, читает два первых
сектора в FRAM и снимает overlay через SD control bit 2 и чтение адреса 0.
В cold-boot test нет записи CPU регистров или подмены PC тестбенчем.

## IRQ, CSR и границы совместимости

uJ11 принимает **уже разрешённый vector на IRQ ACK edge**. В AM4 он выдавался
в ходе другой последовательности подтверждения; прямое соединение было бы
неверным. Board bus теперь выдаёт vector до ACK, с приоритетами UART=4,
RK=5, KW11=6. Private RK assist имеет vector **160000** и отдельное разрешение
при IPL7: RT-11 bootstrap запускает дисковую команду с IPL7. Это внутренний
firmware assist, а не обход IPL для обычных внешних IRQ. Проверены сохранение
frame/PSW/SP, возврат RTI, маскирование внешнего IRQ и полный 16-bit vector.
Default core interface по-прежнему использует 8 vector bits без этого исключения.

Добавлен read-only **MAINT 177750 = 000031 octal**. RT-11 после MFPT=5 читает
этот CSR; отсутствие ACK приводило к рекурсивным bus faults. Поля взяты из
DEC *KDJ11-A CPU Module User's Guide*, §2.4, table 2-6: module ID=1,
FPA=0, HALT trap option=1, POK=1; остальные выбранные поля равны нулю.
[Первичный DEC manual](https://ftpmirror.your.org/pub/misc/bitsavers/www.computer.museum.uq.edu.au/pdf/EK-KDJ1A-UG-001%20KDJ11-A%20CPU%20Module%20User%27s%20Guide.pdf).
Это ограниченный board identification profile, не обещание всех возможностей
KDJ11-A. PSW mode/register banking и mapped PSW 177776 не добавлены.
Верхние PSW bits могут сохраняться как данные, но не переключают банки/режимы;
architectural проверки по-прежнему ограничены kernel profile CP27. Отсутствующие
MMU/system CSR дают bus error, как при RT-11 hardware probing.

KL11, SD service и RK firmware взяты из изученного lsi11-fpga. High-byte CSR
writes, panel lanes, SD read side effect, RK bank, resolved vectors и DMA/RTI
overlay имеют отдельные тесты. Полный RK611 controller, native ODT, SD failure
console и arbitrary-OS compatibility не заявлены. Bootstrap HALT использует
нынешний документированный DCJ11 restart profile; AM4 ODT автоматически не перенесён.
[Исходники и hashes](cp28-source-audit.json).

KW11 сохраняет тот же период **591200 CPU clocks**. Его скрытый двоичный
счётчик заменён на 20-bit Galois LFSR с вычисленным terminal state. Период
полинома проверен по всем 1048575 ненулевым состояниям; для divisors 1, 2, 3,
257 и 591200 проверены точные такты событий и reset. Не используются LFSR
значения как случайные задержки; интервал детерминированный. Поддерживаемый
DIVISOR: 1..1048575.

## Datapath и dispatch

36-bit v12 microcode **не менялся: 954/1024 words, 349 labels**. Q, RF16×16 и
микросеквенсор сохраняют CP27 архитектуру. ALU разделён на четыре общих
выходных пути: arithmetic, Boolean, left, right. SAT доказывает эквивалентность
всех A/B/op/carry/byte combinations с CP27; потеря carry намеренно отвергается.
Четырёхзначная RF reset simulation и 66592 независимых ALU checks также прошли.

Board выбирает `ROM_DECODE=1`: **один 1024×9 EBR**, exact entry, без последующего
большого combinational decoder. Адрес ROM содержит только значащие для dispatch
биты opcode: source/destination mode-zero для double operands, поля single/
branch, специальную EIS группу и все 256 low-byte system opcodes. Генератор
проверяет все 65536 opcode на коллизии; используются 601 ROM row. Portable и
настоящая Lattice DP8KC модель совпали с CP27 на всех opcode и enable holds.

Успешный physical FETCH ACK захватывает IR/MDR и запускает decoder ROM. Следующий
внутренний clock коммитит PC+2 и dispatch; memory request в этот clock снят.
Failed/odd FETCH не ждёт decoder и сохраняет прежний fault path. Для ideal
zero-wait RAM MOV/ADD/CMP/BR требуют **3 clocks** вместо 2 в default combinational
core. Дополнительный clock не меняет число memory beats. Prefetch/SPI throughput
в полном top ещё нужно оптимизировать; старые CP27 counts не выдаются за CP28.

## Проверенный запуск

Образ `rt11v503.dsk`, 27540480 bytes, SHA-256
`e769228f2e1262220297bfa98b8f2841688849ab4c49ad9cd48d0d73d0a99553`.
SD model открывает backing file read-only, записи сохраняются в RAM overlay;
SHA исходного образа до/после одинаков. STARTF.COM содержит две команды,
поэтому ввод DIR начинается после **третьего** приглашения.

Получены banner `RT-11FB (S) V05.03`, каталог **98 Files, 2201 Blocks**,
**51455 Free blocks** и возврат к приглашению. Сверены **3270 bytes реального
UART TX**, включая stop bits. Это не только наблюдение CSR writes.

| Показатель полного cold boot + STARTF.COM + DIR | Результат |
|---|---:|
| CPU clocks | 355132188 |
| Retirements, включая bootstrap/RK firmware | 3983731 |
| CPU read / write beats, включая error responses | 5215901 / 423446 |
| FRAM CS assertions | 3393830 |
| SD sectors read / written | 162 / 6 |
| KW11 events | 576 |
| RK CSR writes | 300 |
| Средние clocks / retirement | 89.145625 |
| Расчёт при nominal 29.56 MHz | 12.013944 s; 331592 retirements/s |

Это simulation counts и расчёт, не замер платы. Workload включает cold startup,
firmware и ожидания устройств, поэтому не является чистым ALU benchmark.
[UART transcript](../tb/reports/cp28/cp28-uart.txt),
[полный log](../tb/reports/cp28/cp28-board-rt11.log),
[manifest с исходниками, fixtures и reports](verification-cp28.json).

CP28 также прошёл 62449 integer cases, 32780 bus-fault cases через synchronous
fetch, 23840 FIS cases (в том числе 3072 injected faults), 4096 FRAM transactions,
29 directed peripheral beats, два private IRQ profiles и default-core regression.
Полная прежняя integer vendor suite и vendor **whole-board** RT-11 не запускались;
vendor gate здесь проверяет firmware/decoder ROM. Это явно ограниченный набор.

## Сравнение и продолжение

Исторический AM4 board от 2026-09-05 описан в lsi11-fpga PORTING-NOTES:
1271 LUT, 639 slices, 7 EBR, физическая RT-11/DIR проверка при 29.56 MHz.
CP28 имеет на 54 LUT, 29 slices и 1 EBR меньше, но uJ11 физически не проверялся.
Сопоставимого AM4 cold-boot/DIR cycle log здесь нет; скоростной выигрыш не заявлен.
CP27b (1095 LUT/416 FF/4 EBR) включал prefetch и probe, но не полный board;
разница с CP28 не является чистой стоимостью периферии.

До расширения FP11 нужны дальнейшее снижение LUT, подключение и измерение
instruction-stream prefetch в общем top, directed disk error/file-write/Ctrl-C
проверки, external pin timing/OSCH tolerance и физическая проверка. Цель
900–1100 LUT остаётся невыполненной. Полный FP11 не реализован и его fit не
доказан. MMU не проектируется и EBR под него не резервируется.

## Воспроизведение

```sh
make -C ../microasm11
make board
make test-board-units            # LATTICE_SIM_DIR задаёт DP8KC/GSR/PUR
make test-board-isa
make test-board-rt11             # другой образ: tools/run_board.py --image PATH
make verify-cp28 YOSYS=/path/to/yosys
# Linux/Diamond; обязательно новое имя implementation:
make synthesis-board BOARD_CHECKPOINT=cp28n
```

`verify-cp28` сверяет итоговый архив CP28m с текущими synthesis inputs. После
изменения RTL нужен новый synthesis gate и осознанное обновление baseline.
Python 3, Icarus, Verilator, существующий C oracle/microasm11 и vendor simulation
files требуются отдельно. Yosys использован 0.69 (yowasp-yosys 0.69.0.0.post1233).
FPGA не программировалась; external input/output delays ещё не заданы в LPF.
