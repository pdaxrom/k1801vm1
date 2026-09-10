# CP33 — PAR/PDR в одном EBR

Изолированный блок хранения APR и decode их физических адресов реализованы
и проверены. **К production CPU они ещё не подключены.** Полная сборка
сохраняет baseline CP31c, FPGA остаётся CP29a. Исходники, test corpus и
raw synthesis reports связаны hashes в [verification-cp33.json](verification-cp33.json).

## Контракт по DEC

Источник: [DCJ11 User's Guide, EK-DCJ11-UG-PRE](https://www.bitsavers.org/pdf/dec/pdp11/1173/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf),
§4.5.1, §4.5.2 и карта §4.9. PAR хранит все 16 бит, в том числе старшие
разряды для 22-bit mapping. В PDR сохраняются BC<15>, PLF<14:8>, ED<3> и
ACF<2:1>. Зарезервированные <7,5,4,0> читаются нулями; W<6> не задаётся
software write. Любая непустая запись PAR или PDR, в том числе одного
старшего байта, сбрасывает W парного PDR. BC хранится для readback;
cache в uJ11 отсутствует.

Карта canonical PA22, восьмеричные адреса; каждая строка содержит 32 words:

| Набор | PDR I | PDR D | PAR I | PAR D |
|---|---|---|---|---|
| Supervisor | 17772200–17772216 | 17772220–17772236 | 17772240–17772256 | 17772260–17772276 |
| Kernel | 17772300–17772316 | 17772320–17772336 | 17772340–17772356 | 17772360–17772376 |
| User | 17777600–17777616 | 17777620–17777636 | 17777640–17777656 | 17777660–17777676 |

Decode принимает оба байта каждого регистра; всего 192 byte addresses.
Он не принимает нижние PA16/PA18 aliases. Выбор byte lanes, canonical
mapping при выключенном/18-bit MMU, odd-word priority и проверка privilege
остаются обязанностями будущего CPU/bus controller.

## Хранение и доступ

[`uj11_mmu_apr_ram.v`](../rtl/uj11_mmu_apr_ram.v) явно использует один DP8KC
с двумя непересекающимися x9 ports: low/high bytes одного 16-bit word.
Это тот же проверенный способ byte enables, что у firmware EBR CP31c.
Используются 128 words — 64 пары PAR/PDR. Архитектурно через CSR доступны
48 пар K/S/U × I/D × 8 pages. Остальные 16 пар, соответствующие mode=2
в шестибитном индексе, не имеют CSR адресов; это не поддержка mode=2.
Размер EBR физически не меняется от этих неиспользуемых slots.
Основной RF16×16 и содержимое microstore не меняются.

[`uj11_mmu_apr.v`](../rtl/uj11_mmu_apr.v) сериализует редкие изменения APR.
Сначала читает нужный word; для модификаций читает PDR, затем сохраняет
его controls с очищенным W. Запись PAR выполняется следующим тактом.
Для отдельной команды `mark_written` меняется только W, без изменения
PAR и PDR controls. Непустая CSR write имеет приоритет над `mark_written`.
Массив W в FF не требуется. Во всём контроллере только **3 state FF**.

| Операция | Тактов от принятия request до ACK |
|---|---:|
| Read PAR или PDR | 2 |
| Write PDR, включая byte write | 2 |
| Explicit mark W | 2 |
| Write PAR с очисткой парного W | 3 |

Request и payload удерживаются до ACK. После ACK запрос должен сняться;
до его снятия новый доступ не начинается. Такт освобождения request не
включён в таблицу. `byte_enable=00` означает отсутствие CSR write;
это не обращение к несуществующему byte lane. Данные на ACK используются
только для read. Во время write EBR output не определяет readback.

FPGA configuration инициализирует store нулями как решение реализации;
это не утверждение о начальных PAR/PDR настоящего J-11. Reset выключает
EBR access и отменяет незавершённые фазы; память сохраняется. Если reset
пришёл между очисткой PDR.W и записью PAR, уже очищенный W сохраняется,
а незавершённая запись PAR не выполняется. Контроллер не обещает rollback
уже выполненных записей. Эти границы проверены отдельно.

`mark_written` пока задаётся вызывающим testbench. Момент его выдачи при
реальном memory access, включая fault/abort, здесь не реализован. Наличие
этой команды не означает готовность автоматического PDR.W в CPU MMU.

## Измерения HC1200

Diamond 3.14, `LCMXO2-1200HC-4SG32C`, constraint 29.56 MHz. Каждый вариант
имеет отдельные source snapshot и MAP/PAR/TRACE reports; все gates PASS.

| Вариант | LUT4 | FF | EBR | Slices | TRACE MHz |
|---|---:|---:|---:|---:|---:|
| [CP33a](../synth/reports/cp33a/result.json), APR без CSR decode | 31 | 49 | 1 | 24 | 112.994 |
| [CP33b](../synth/reports/cp33b/result.json), APR + canonical CSR decode | 41 | 65 | 1 | 32 | 106.963 |
| [CP33c](../synth/reports/cp33c/result.json), упрощённые address/WE selects | 40 | 65 | 1 | 32 | 96.862 |

CP33a содержит 46 измерительных FF, b/c — 62; только 3 FF относятся
к APR controller. В CP33c учитываются don't-care address/WE при выключенном
EBR enable, поэтому часть полных state decoders заменена прямыми state bits.
Экономия — один LUT; timing ухудшился, но остаётся выше clock constraint
и цели 50 MHz для этого отдельного блока. По приоритету площади оставлен c.
Это не Fmax CPU с MMU. Hierarchical counts сохранены в `design.areasrr`;
они не являются дополнительным board LUT cost и не складываются с MAP counts.

Production inputs побайтно совпадают с CP31c: **1252 LUT / 326 FF / 6 EBR /
628 slices / 30.917 MHz**, microstore 954/1024×36 v12. В полной сборке
свободны только **28 LUT / 12 slices / 1 EBR**. Отдельные CP32 translator
и CP33 APR вместе с MMR/abort нельзя считать помещающимися на HC1200.
Перед полной интеграцией нужен измеренный вариант с сокращением общей
логики — в частности, исследование использования существующих ALU и
микросеквенсора для relocation/PDR checks. Их пригодность и экономия
ещё не доказаны. Большой hardwired MMU поверх почти полного core не добавлен.

## Проверки

* **1144373 commands** на каждом portable/vendor варианте a и c. Перебраны
  все значения PAR16/PDR16, все 64 storage entries, все byte patterns и
  masks. Проверены W set/clear, high-only write, сохранность соседнего
  регистра, reserved bits, idle/held request, cold init и reset boundaries.
* **4194304 PA22 bytes** в CSR decode test: ровно 192 совпадения, все
  mode/I-D/page/PAR-PDR selectors правильны, нет mode2 и нижних aliases.
* **65536 C differential cases / 196608 CSR commands** на portable и vendor
  DP8KC. Существующий `../core/core.c`, `ENABLE_MMU=1`, DCJ11, его реальные
  CSR word/byte helpers и W helper задают эталон. После операции сравниваются
  оба регистра пары. Затем реально прочитанные из EBR PAR/PDR подаются
  в CP32 translator: 4149 успешных PA и 61387 read-fault результатов совпали.
  Для C локальная 18-bit I/O page приводится к canonical PA22.
* Strict `--Wall` lint текущих APR/probe/CSR RTL прошёл. Lattice DP8KC/GSR/PUR
  используются без исправлений или замены поведения.

Serial lookup в differential test сохраняет PAR/PDR в **testbench latches**.
Это связывает проверку EBR и translator, но не является синтезированной
MMU pipeline. Автоматический выбор APR по PSW/VA, MMR0/1/2/3, SP/register
banks, freeze/restart, MMU abort vector250 и physical RK DMA ещё отсутствуют.
Поэтому RT-11XM из `../lsi11/disks/rt11v5.3/system.dsk` ещё не запускался
с MMU. Исходный образ не изменён; его hash проверяется manifest script.

Повторение проверок:

```
make test-mmu-apr test-mmu-apr-decode test-mmu-apr-oracle
make vendor-mmu-apr LATTICE_SIM_DIR=build/vendor
```

Для synthesis — `tools/checkpoint.py cp33c --mhz 29.56` в свежей Linux/Diamond
копии. `tools/record_cp33.py` проверяет отчёты и архивирует готовые журналы;
он не запускает тесты повторно и не прошивает FPGA.
