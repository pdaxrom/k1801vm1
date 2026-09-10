# CP44 — CPU relocation в 128 КиБ FRAM, отказ по площади HC1200

Экспериментальный CPU выполняет kernel unified PAR relocation в режимах
18/22 bits и обращается ко всем 128 КиБ FRAM. Проверены настоящий CPU,
общий APR EBR, board bus и SPI FRAM model. **Ни один полный HC1200 top
не поместился.** Лучший результат — 1351 LUT / 359 FF / 7 EBR / 676 slices.
CP44 сохранён как проверенный частичный прототип и area evidence, в рабочую
сборку не принят. Production CP40h, принятый APR experiment CP43d и
физическая плата CP29a прежние.

## Реализованная граница

Контракт основан на DEC DCJ11 User Guide §4.5, §4.7.1, §4.7.4:
[локальный оригинал](../../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf),
[OCR документа DEC](https://dusted.dk/pages/computers/J-11/datasheet-DEC-DCJ11_Microprocessor_Users_Guide_OCR.pdf).

- VA16, PAR16, canonical PA22. Выбор PAR — kernel I page `VA[15:13]`.
  `block = PAR + VA[12:6]`, byte offset `VA[5:0]` сохраняется.
- MMR0<0> выключен: VA переносится в PA, I/O page `160000..177777`
  нормализуется в `17760000..17777777` (octal).
- MMR0<0> включён: MMR3<4> выбирает 18/22 bits. В 18-bit режиме сумма
  ограничивается 18 bits, верхняя I/O page канонизируется. В 22-bit режиме
  сумма ограничивается 22 bits, без превращения PA `0x3e000` в I/O.
- RAM — PA `0x00000..0x1ffff`; FRAM bank формируется из PA16.
  Полный PA проверяется до сужения до 17 SPI address bits. Остальные
  нераспознанные адреса дают существующий bus fault/vector4, без alias RAM.
- MMR0 — canonical PA `17777572`, unmapped VA `177572`; software write/read
  bits 15:13 и 0, word/byte lanes, RESET. Software flags сами не вызывают trap.
  Остальные read bits пока нулевые: аппаратные fault/page fields не подключены.
- MMR3<4> подключён к выбору ширины. Остальные сохранённые MMR3 controls
  пока не управляют modes/I-D/CSM/MAP.

**PDR protection/length/automatic W, MMR1/2, аппаратные MMR0 fault/page
metadata, abort250/freeze/restart, mode/SP switching и separate I/D отсутствуют.**
Изолированный CP32 PDR checker здесь не используется: CP44 измеряет
стоимость подключения relocation и PA22, до добавления защиты.

## Два способа вычислить PA

**A: общая ALU и микрокод.** Новый backend `microasm/uj11relocasm.py`
расширяет существующий подход, исходный `microasm11` не меняется.
16 helper words добавлены в свободные адреса; исходные 954 guest words и
labels сохраняются, всего 970/1024×36. T5 сохраняет PSW, T6 принимает PAR,
T7 получает VA, шесть LSR и AND127 формируют VA[12:6], ADD даёт block sum.
MMU_RETURN сохраняет нормализованный block16 и восстанавливает исполнение
исходной memory microinstruction. Q/MDR и EA CALL link сохраняются.
Удерживается четырёхбитный исходный A selector, RF не расширяется.

**B/D/E: отдельный небольшой сумматор relocation.** Native CPU, datapath,
ALU, microsequencer и 954-word microcode не меняются. Bridge перед board bus
получает PAR через общий EBR, захватывает PA block, освобождает APR port
и выдаёт physical request. Это отдельная MMU-операция; PDP-11 addressing
modes остаются микрокодными. Полный board получается на 43 LUT / 14 FF
меньше A и экономит 15 clocks на каждый mapped beat.

У обоих вариантов решение mapped/bypass удерживается до ACK. Иначе запись
MMR0, включающая MMU, могла бы повторно начать тот же запрос с трансляцией.
Проверен этот случай при KIPAR7, указывающем в RAM, а также выключение MMU
через другую virtual I/O page. Сохранённый PA не меняется после записи
MMR0/MMR3/APR в ходе уже начатого physical запроса. VA/control/data держит CPU.
Direct bridge требует два дополнительных clocks на mapped beat без
внешних задержек, microcoded — 17; unmapped запрос не читает PAR.

**C: registered RAM/I/O classification.** Попытка перенести полную PA22
проверку до holding registers уменьшила FF на три, но увеличила LUT на 17.
Отклонена. `--classified` сохраняет этот вариант; default builder использует
полный PA22 bus. Выходы классификации в default не подключены и удаляются
синтезом. Комментарий в bridge про их использование относится к C.

## Firmware, DMA и периферия

Bootstrap overlays доступны только в физическом нижнем банке. Private RK
ROM выбирается только в canonical I/O. Для firmware fetch в его служебном
окне и MOVB copy operand существует bypass, зависящий от raw CPU VA и
направления memory microinstruction. Это исключает зависимость bypass от
ещё не сформированного physical запроса. Trap stack остаётся mapped.

RK firmware по-прежнему исполняет PDP-11 инструкции; private MOVB copy
обращается к физическим нижним 64 КиБ, включая численные I/O aliases без
срабатывания CSR. Старшие разряды DMA не добавлены. RGB/HDSP/keyboard/HG,
UART, SD, KW11 и SPI transport остаются в полном synthesis top.
На настоящей плате CP44 не запускался.

## Пять полных synthesis gates

Diamond 3.14.0.75.2, LCMXO2-1200HC-4SG32C, внутренний clock constraint
29.56 MHz, те же board pins/strategy. External pin delays не заданы.

| Gate | Изменение | LUT4 | FF | EBR | Slices | Words | Результат |
|---|---|---:|---:|---:|---:|---:|---|
| CP43d | Принятый APR + MMR3 baseline | 1258 | 351 | 7 | 630 | 963 | PASS, 30.866 MHz |
| [CP44a](../synth/reports/cp44a/result.json) | Microcoded relocation | 1394 | 373 | 7 | 699 | 970 | MAP FAIL |
| [CP44b](../synth/reports/cp44b/result.json) | Direct bridge | 1351 | 359 | 7 | 676 | 954 | MAP FAIL |
| [CP44c](../synth/reports/cp44c/result.json) | Registered region classes | 1368 | 356 | 7 | 685 | 954 | MAP FAIL |
| [CP44d](../synth/reports/cp44d/result.json) | Вернуться к PA22 bus | 1351 | 359 | 7 | 676 | 954 | MAP FAIL |
| [CP44e](../synth/reports/cp44e/result.json) | Финальный strict-lint MMR0 input handling | 1351 | 359 | 7 | 676 | 954 | MAP FAIL |

MAP требует больше 1280 LUT и 640 slices, PAR/TRACE не выполнялись.
**Fmax CP44 неизвестен.** B/D/E повторяют площадь; E соответствует текущим
input hashes. Архив каждого gate содержит собственный source snapshot и
raw reports, включая неудачные варианты. Проверенный production CP40h:
1159 LUT / 326 FF / 6 EBR / 584 slices / 31.470 MHz.

Hierarchical Synplify reports помогают выбрать следующий объект анализа:
board bus ORCALUT4 меняется с 485 (CP43d) на 583 (E), engine — с 566 на 538;
bridge содержит 29 ORCALUT4 и 9 CCU2D. Это примитивы до MAP, не аддитивная
стоимость отдельных функций в LUT4. Нельзя приписать весь рост одной
проверке адреса. Сравнение полного top показывает, что sharing общей ALU
не окупает его context/control overhead в A.

До границы устройства лучшему частичному варианту не хватает **71 LUT /
36 slices**; свободных EBR нет. Это ещё без защиты/restart. Следующий gate
должен уменьшить стоимость полного bus/decode/control, сохраняя всю
периферию и проверку PA22 до truncation. Расширение MMU приостановлено до
измеренного резерва; простого fit на 1280 LUT недостаточно для обвязки.

## Verification и измеренные clocks

| Проверка | Результат |
|---|---|
| Direct CPU, portable | 4096 words × 8 pages × 2 modes; 67 468 380 clocks |
| Microcoded CPU, portable | Тот же workload; 76 319 820 clocks |
| Direct / microcoded CPU, vendor EBR | 4 words × 8 pages × 2 modes; 97 692 / 110 412 clocks |
| CPU edge cases | Direct portable/vendor, microcoded portable: PASS |
| C relocation oracle | 262144 addresses, все PAR16; 196608 held lookup edges |
| C MMR0 software control oracle | 262160 commands, включая 16 RESET |
| Full bus | 32 transaction checks, 6291456 PA/private-service combinations |
| Strict Verilator --Wall | Три новых RTL units: PASS |

Полные CPU runs записывают и читают каждый word верхних 64 КиБ в обоих
режимах и сравнивают snapshot всего нижнего банка. APR/MMR программируются
инструкциями CPU; только bootstrap overlay отключён тестом. У обоих
590096 PAR reads и 8 control beats; разница clocks = 15 × 590096.
Это directed memory workload, не универсальное ISA CPI.

Edge tests проверяют 18-bit wrap, 22-bit NXM, различение 18/22 I/O,
MOVB lane/sign extension, odd word read/write vector4 до external bus,
mapped trap stack в верхней FRAM без повреждения нижнего alias и выполнение
opcode/immediate stream из верхнего банка сразу после включения MMR0.
Oracle использует DCJ11 helpers из неизменённого `core/core.c` с valid PDR;
это сравнение relocation/control, не полное CPU/MMU differential.
Bus sweep проверяет все 2^22 PA и 32 private-service states × 2^16 VA.

Cold RT-11FB + DIR на direct board: **415159611 clocks**, 4311823 retirements,
5675704 reads / 489280 writes, 3980328 FRAM transactions, 300 RK commands,
576 timer edges, 3270 UART bytes, 162 SD reads / 6 RAM-overlay writes.
UART transcript byte-identical CP43. Новый счётчик зарегистрировал
526239 mapped beats, 65664 upper-FRAM beats и 4 MMR0 writes: этот запуск
действительно использует relocation. Поэтому его execution path уже не
совпадает с CP43; прежние counts не используются как speedup baseline.
Точное распределение разницы между startup и interrupt timing не измерено.

Во время включённой MMU счётчики private ROM/DMA равны нулю. Cold FB не
доказывает корректность активной RK-команды при MMU-on или high DMA.
Full FIS corpus не перезапускался: в direct варианте CPU/ALU/microcode
не менялись; полная FIS регрессия для отвергнутого A также не заявляется.

**RT-11XM не загружена.** `../lsi11/disks/rt11v5.3/system.dsk` сохранён,
SHA256 `9350c62f50e2713f56904b7222f2d829f6bf020cd67e28562e3252a48b6993dd`.
FB disk также неизменён, все SD writes идут в RAM overlay.

## Воспроизведение и архив

Из корня `uJ11-fpga`, после обычной генерации production firmware/microcode
и установки существующих portable/vendor simulation dependencies:

```sh
python3 tools/build_direct_relocate_cp44.py
python3 tools/check_relocate_units_cp44.py
python3 tools/check_relocate_cp44.py --direct
python3 tools/check_relocate_cp44.py
python3 tools/check_relocate_cp44.py --direct --vendor --words 4
python3 tools/check_relocate_cp44.py --vendor --words 4
python3 tools/check_relocate_cp44.py --direct --edges
python3 tools/check_relocate_cp44.py --direct --vendor --edges
python3 tools/check_relocate_cp44.py --edges
python3 tools/check_relocate_bus_cp44.py
python3 tools/run_relocate_cp44_board.py
```

Direct builder генерирует также microcoded comparison inputs. Для C нужен
`--classified`; после него default inputs необходимо пересобрать до тестов.
Synthesis на Linux с Diamond запускается `tools/checkpoint_relocate_cp44.py`
с новым уникальным именем gate и `--direct` для direct варианта. Старые
gates/source manifests не перезаписывать. A–E сохранены в `synth/reports/`.

`tools/record_cp44.py` проверяет все пять архивов, hashes текущего E,
неизменность hardware inputs CP40h/CP43d, passing logs и disk hashes.
Его запускают только с inputs, соответствующими данному checkpoint.
[Manifest](verification-cp44.json), [test archive](../tb/reports/cp44/)
содержат logs, compressed C corpora и exact test sources. Vendor models
имеют hashes в manifests и не включены в source archive.
