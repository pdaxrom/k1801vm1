# CP39 — CPU-доступ к PAR/PDR и общий EBR

Экспериментальный полный board **CP39d: 1268 LUT4 / 344 FF / 7 EBR /
635 slices / 31.075 MHz**, MAP/PAR/TRACE PASS при 29.56 MHz. CPU читает и
записывает APR обычными MOV/MOVB, микрокодный lookup использует ту же память.
Относительно read-only CP38g добавлено **40 LUT, 3 FF, 0 EBR**.
Свободны лишь **12 LUT и 5 slices**. Это измеренная стоимость доступа к APR,
а не fit полной MMU: дальнейшая интеграция требует сокращения общей логики.

Production остаётся CP38f: **1188 LUT / 326 FF / 6 EBR / 30.943 MHz**.
На плате CP29a; CP39 не прошивался. FIS, FRAM, UART/KW11, HDSP/RGB/keyboard/HG,
SD и RK firmware сохранены. Верхние 64 КиБ FRAM ещё недоступны CPU.
Исходники и журналы связаны hashes в [verification-cp39.json](verification-cp39.json).

## Граница реализации

[`build_mmu_apr_csr.py`](../tools/build_mmu_apr_csr.py) создаёт только
`build/cp39-csr/`: адаптированные core/engine, board/bus и контроллер общего
порта. Он проверяемыми заменами переиспользует CP33 APR controller и CP37
lookup backend; рабочие RTL и микрокод не меняются. Все 954 guest words и
9 helper words сохранены: **963/1024×36**, дополнительных слов нет.

Контракт регистров взят из локального
`doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf`, §4.5.1/4.5.2/4.9;
[первичный DEC manual](https://www.bitsavers.org/pdf/dec/pdp11/1173/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf).
PAR хранит все 16 бит. PDR хранит BC, PLF, ED, ACF; reserved bits читаются
нулём, W нельзя установить записью software. Любая непустая запись PAR/PDR,
включая high-byte-only, очищает W парного PDR. Полные masks и карта
приведены в [контракте CP33](mmu-apr.md).

Через CPU доступны все **96 CSR words / 48 pairs** K/S/U × I/D × 8 pages.
Это доступ к таблицам, а не реализация processor modes: CPU по-прежнему
имеет один kernel register set, lookup выбирает kernel unified page.
MMR0/1/2/3, translation, PDR checks, automatic W updates, abort250/restart,
mode/SP switching и I/D selection пока отсутствуют. Отдельный проверенный
translator CP32 поддерживает 18/22 bits, но не включён в этот CPU gate.

## Доступ и арбитраж

Один DP8KC хранит APR; два непересекающихся x9 ports обслуживают low/high
байты одного слова. RAM вынесена из engine в board, её read output поступает
и на CPU bus, и на существующий D=APR микрокодный вход. Main RF остаётся 16×16.

CSR controller владеет портом до request-low edge после ACK. Lookup получает
комбинационный grant только при `!reset && !request && !busy`; при конфликте
микросеквенсор удерживает APR_READ. Дополнительных owner/data registers нет,
состояние контроллера — прежние 3 FF CP33. В обычной CPU-последовательности
lookup завершается до внешнего запроса, и конфликта нет. Это проверено на
реальном board CPU; независимый port test проверяет также столкновения.

| Операция | Тактов |
|---|---:|
| Lookup, от grant edge до доступного EBR output | 1 synchronous read edge |
| CSR read | 2 |
| CSR write PDR | 2 |
| CSR write PAR с очисткой парного W | 3 |
| Весь прежний helper на guest memory word без hold | 10 дополнительных |

CSR latency — контракт контроллера, без освобождения request и CPU/board
turnaround. Helper сохраняет данные на следующей ALU-микрокоманде. Explicit
mark-W остаётся в проверяемом controller API, но в board привязан к нулю;
автоматическая установка W ещё не реализована.

Board пока имеет немаппированный 16-bit bus. Для APR decoder CPU I/O address
приводится к canonical PA22, одновременно проверяется `cpu_io_page`.
Это локальный decode CSR, не 22-bit physical CPU interface.
Существующий physical RK MOVB operand имеет приоритет над CSR, включая
совпадающий адрес APR. Byte lanes формируются штатным board adapter;
нечётное word обращение отклоняет `uj11_mem` до выдачи bus request.

Reset отменяет незавершённые фазы, сохраняет EBR и уже выполненные записи;
rollback не обещается. FPGA configuration задаёт нулевой init как решение
uJ11, а не как заявленное начальное состояние DEC J-11.

## Реальные измерения HC1200

Diamond 3.14.0.75.2, LCMXO2-1200HC-4SG32C, constraint 29.56 MHz.
External pin delays не заданы; TRACE не заменяет проверку физической платы.

| Gate | Возврат данных APR | LUT4 | FF | EBR | Slices | TRACE MHz |
|---|---|---:|---:|---:|---:|---:|
| [CP39a](../synth/reports/cp39a/result.json) | Masked OR среди малых устройств | 1292 | 344 | 7 | 647 | — |
| [CP39b](../synth/reports/cp39b/result.json) | APR перед firmware/FRAM mux | 1268 | 344 | 7 | 635 | 31.075 |
| [CP39c](../synth/reports/cp39c/result.json) | APR после firmware/FRAM mux | 1327 | 344 | 7 | 666 | — |
| [CP39d](../synth/reports/cp39d/result.json) | Финальный вариант b, явные объявления портов | 1268 | 344 | 7 | 635 | 31.075 |

A/C превысили LUT и slices на MAP, Fmax не получен. B/D полностью прошли
MAP/PAR/TRACE. Выбран порядок `APR → firmware → FRAM → small devices`:
он экономит 24 LUT относительно masked OR. Менялось положение mux, а не
набор устройств или ISA. Все четыре raw/source archives сохранены;
неудачные gates не подменены успешными.

## Проверки

- На portable RAM и неизменённом vendor DP8KC: **1 144 373 CSR commands**
  и **2 288 746 coherent lookup reads** в каждом run. Полные PAR/PDR values,
  все storage entries, byte masks, W set/clear, held requests, collision,
  release, reset и частично выполненные записи.
- На обоих вариантах реальный CPU/board/SPI FRAM выполнил программу MOV/MOVB:
  **432 readbacks, 720 CSR beats, 4902 lookup reads, 221988 clocks**;
  все 48 пар, оба байта, MOVB sign extension. Программа пересекает границу
  VA page при ненулевых APR. Отдельно проверены odd-word vector4, отсутствие
  CSR write и чтение сохранённого PAR после reset. Завершение через WAIT:
  HALT в существующем профиле выполняет restart, а не служит тестовым STOP.
- На обоих вариантах board bus: **33 beats** с существующей периферией и
  **2 097 152 address/service/direction/fetch combinations**. Physical DMA
  записывает FRAM по адресу APR, оставляя CSR неизменным.
- Sequential CPU miter: **69632 cases**, все 88 memory words и 8 APR pages;
  **781100 PAR/PDR reads**, 390550 entry/return pairs, 1607247 held edges.
  Сравниваются guest RF, Q, PSW, MDR, IR, CALL, uPC и bus behavior; reference
  clock пропускает только private helper edges. Reset в девяти позициях.
- Strict `--Wall` lint: core default, aligned-word ROM decode и shared APR.
  Четыре внесённых дефекта обнаружены: PAR/PDR swap, нарушение ownership,
  потеря W clear и перехват physical DMA.
- Cold **RT-11FB + DIR** на реальном RTL board с моделями FRAM/SD:
  **412130048 clocks, 3987390 retirements, 5222610 reads, 424452 writes,
  3397976 FRAM transactions, 300 RK commands, 663 timer edges, 3270 UART bytes,
  162 SD reads / 6 writes**. Counters и UART побайтно совпали с CP38g.

Icarus logs сохраняют прежние предупреждения CP35 о расположенных ниже
объявлениях однобитных `running`/`advance`, inherited timescale импортированного
UART и предупреждения неизменённого Lattice model. Новые APR board ports
объявлены до использования; strict Verilator lint проходит без waivers.

Это simulation и synthesis; **RT-11XM ещё не загружен**. Пользовательский
`lsi11/disks/rt11v5.3/system.dsk` не изменён, SHA256 проверен manifest script.
Следующий gate — измеримое сокращение общей LUT cost перед relocation/MMR,
затем CPU PA22/128 КиБ и extended RK DMA. Запас 12 LUT недостаточен для
заявления о пригодности текущей полной архитектуры MMU.

Повторение из `uJ11-fpga`:

```sh
python3 tools/build_mmu_apr_csr.py
python3 tools/check_mmu_apr_csr.py port
python3 tools/check_mmu_apr_csr.py port --vendor
python3 tools/check_mmu_apr_csr.py bus
python3 tools/check_mmu_apr_csr.py bus --vendor
python3 tools/check_mmu_apr_csr.py cpu
python3 tools/check_mmu_apr_csr.py cpu --vendor
python3 tools/check_mmu_apr_csr_miter.py
python3 tools/check_mmu_apr_csr_lint.py
python3 tools/check_mmu_apr_csr_negative.py
python3 tools/run_mmu_apr_csr_board.py
```

Vendor files ожидаются в `build/vendor/{DP8KC,GSR,PUR}.v`. Синтез выполняет
`tools/checkpoint_mmu_apr_csr.py cp39d` только в свежей Linux/Diamond копии;
архивирование — `tools/archive_synthesis.py`, сверка evidence —
`tools/record_cp39.py`. Исторические отчёты после изменения inputs не
пересоздавать; для нового варианта нужен новый gate.
