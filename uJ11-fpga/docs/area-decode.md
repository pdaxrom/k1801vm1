# CP36 — opcode ROM и выравнивание byte operands

Рабочий полный HC1200 top уменьшен с **1252 до 1222 LUT**, сохранив
326 FF, 6 EBR и прежнее число тактов. TRACE Fmax вырос с 30.917 до
31.116 MHz. Вариант со служебным входом CP35 уменьшен с **1273 до
1243 LUT**: теперь свободны 37 LUT, 14 slices и один EBR.
MMU translation/MMR ещё не подключены; FPGA остаётся CP29a.

## Измеренные варианты

Все строки — полный board с FIS, FRAM, UART, таймером, RGB/HDSP/keyboard/HG,
SD/RK firmware, OSCH/reset и прежними pins. Prefetch board выключен.

| Revision | Opcode index / read interface | Context hook | LUT4 | FF | EBR | Slices | TRACE MHz |
|---|---|---|---:|---:|---:|---:|---:|
| CP31c, baseline | Исходный case / byte lanes перед CPU | Нет | 1252 | 326 | 6 | 628 | 30.917 |
| CP35e, baseline | То же | Да | 1273 | 338 | 6 | 638 | 30.498 |
| [CP36a](../synth/reports/cp36a/result.json) | Masked planes / прежние lanes | Нет | 1252 | 326 | 6 | 628 | 30.712 |
| [CP36b](../synth/reports/cp36b/result.json) | Побитовый index / прежние lanes | Нет | 1219 | 326 | 6 | 613 | 29.990 |
| [CP36c](../synth/reports/cp36c/result.json) | Побитовый index / прежние lanes | Да | 1267 | 338 | 6 | 636 | 31.308 |
| [CP36d](../synth/reports/cp36d/result.json) | Побитовый index / aligned word | Нет | 1222 | 326 | 6 | 614 | 31.116 |
| [CP36e](../synth/reports/cp36e/result.json) | Побитовый index / aligned word | Да | 1243 | 338 | 6 | 626 | 31.107 |
| [CP36f](../synth/reports/cp36f/result.json), production | Выбранный вариант в рабочих RTL | Нет | 1222 | 326 | 6 | 614 | 31.116 |
| [CP36g](../synth/reports/cp36g/result.json), experimental | Те же RTL + CP35 | Да | 1243 | 338 | 6 | 626 | 31.107 |

Diamond 3.14.0.75.2 / LCMXO2-1200HC-4SG32C, constraint 29.56 MHz:
все MAP/PAR/TRACE PASS, fully routed. External pin delays не заданы.
Значения — результат полного mapping/placement, не стоимость отдельного
opcode index. Перенос byte mux меняет оптимизацию нескольких конусов логики.
Hierarchical ORCALUT4 включает поглощённую логику, поэтому его нельзя
принимать за число LUT только нового преобразователя адреса.

Без context вариант b на 3 LUT меньше d. Выбран d: при context он даёт
ещё 24 LUT экономии против c, а без context оставляет больший timing margin.
Финальные gates f/g повторяют измерения после переноса в production.
В f worst reported path — microstore EBR → Q[13], 31.683 ns, 17 levels,
59.3% routing, margin 1.691 ns при 29.56 MHz.

У g историческое поле `aligned_word_bus:false` означает отсутствие build
override: production board уже устанавливает `.ALIGNED_WORD_READS(1)`.
Исходный report сохранён; [пояснение и исправление metadata launcher](../synth/reports/cp36g/metadata-notes.md).
RTL и resource/timing данные этим исправлением не менялись.

## Opcode index

Предыдущий decoder собирал весь 10-bit адрес в case по старшему полубайту.
Новый `rtl/uj11_decode_rom.v` задаёт каждый бит явно, используя общие признаки
system-low, single-group и EIS. Например, index[8] равен `~|op[14:12]`,
а index[7] выбирает только между op[7] и op[15]. Отдельный dispatch
для группы F удалён: её reserved opcodes читают уже свободные строки
`0f0/0f4/0f8/0fc` вместо `240`, также содержащие reserved entry `042`.
В этом абзаце адреса hex.

`tools/build_decode_rom.py` использует ту же раскладку и проверяет все
65536 opcode на collision. Адресуются 604 строки вместо 601, но **вся
1024×9 таблица побайтно прежняя**: дополнительные строки и ранее содержали
reserved entry. Microstore, firmware ROM, writable RK storage и все labels
также неизменны. Большой decoder, новый EBR или decode stage не добавлены.

## Read interface

У `uj11_core` добавлен параметр `ALIGNED_WORD_READS`, default=0.

| Параметр | `mem_read_data` | Обработка byte read |
|---:|---|---|
| 0 | Прежний right-justified ответ | Без дополнительного выравнивания |
| 1 | Полное слово по `mem_addr & ~1` | Core выбирает low/high byte по address[0] и обнуляет старшие 8 bits |

HC1200 board включает значение 1 и передаёт `lane_rdata` напрямую.
Opcode ROM читает это слово до byte mux; engine получает прежнее
right-justified значение после mux. FETCH всегда word, что явно задано
в byte qualifier `uj11_engine`; его IR/MDR capture и dispatch не меняются.
Write data, byte selects, odd-word faults, request/ACK и все периферийные
side effects остаются прежними. Ширина адреса всё ещё 16 bits.

Сохранены RF16×16, ALU/Q, engine/sequencer и microinstructions. Это изменение
комбинационного пути данных без FF или тактов ожидания. Старые тестовые
RAM/FRAM interfaces продолжают использовать default=0.

## Проверки выбранных исходников

* Для обеих index-форм: все 65536 opcode и 65536 enable holds на portable
  ROM и неподправленной Lattice DP8KC model в Icarus. Entry сравнивается
  с прежним CP27 combinational decoder. Generator отдельно проверяет
  совпадение всех opcode entries и отсутствие collisions.
* CPU miter с frozen CP31c: кандидат получает реальные обе RAM lanes,
  reference — прежний right-justified ответ. Проверяется кандидат с CP35
  entry и periodic holds; reference пропускает только private clocks.
  69632 cases, все 88 memory words, 390550 entry/return, 1244984 held edges;
  дополнительные clocks точно совпадают с CP35. Дополнительно 1024 Icarus
  cases. Перестановка high/low byte в build-only mutant обнаружена на
  byte-instruction case 37376. Это динамическое сравнение, не formal ISA proof.
* Strict `--Wall` lint: default/word bus × обычный CPU/context hook.
* С прежним FIS oracle и CP35 helper: по 23840 cases / 3072 injected faults
  на RAM и actual SPI FRAM transport с prefetch; 645 cases / 83 faults
  на vendor DP8KC. FIS harness использует прежний default read interface;
  word-bus path покрывается CPU miter и полными board runs.
* Два cold RT-11FB + `DIR` прогона: рабочий board и board с context hook.
  Все счётчики и UART transcript совпадают со своими baselines:

| Workload | Clocks | Retirements | Read / write beats | FRAM transactions | UART bytes | SD reads / writes |
|---|---:|---:|---|---:|---:|---|
| CP36f, как CP31c | 354938300 | 3984366 | 5217011 / 423616 | 3390712 | 3270 | 162 / 6 |
| CP36g, как CP35e | 406268404 | 3986525 | 5221066 / 424227 | 3395982 | 3270 | 162 / 6 |

В обоих 300 RK commands; timer edges — 576 и 654 соответственно.
Записи SD идут в overlay модели, исходные FB/XM images не изменены.
Это functional simulation и отдельный physical fit; FPGA не программировалась.
RT-11XM ещё не запускался.

## Воспроизведение

```
make test-decode-compact          # требует build/vendor DP8KC/GSR/PUR
make test-decode-cpu              # selected word bus + context miter, lint, negative
make test-decode-board            # production cold RT-11FB
make test-mmu-entry-fis           # тот же FIS harness с текущим decoder
make test-mmu-entry-board         # полный board + CP35 helper
python3 tools/record_cp36.py      # hashes, paired counters и архивирование
```

Linux/Diamond: `make synthesis-board BOARD_CHECKPOINT=cp36f` в свежем
implementation directory. Опубликованные отчёты не перезаписывать.
Экспериментальные a–e воспроизводить из их `source.tgz`; build tools используют
frozen CP31/CP35 core snapshots, чтобы перенос в production не менял эксперимент.
Журналы повторно используемых FIS/context harnesses сохраняют префикс cp35,
но их новые manifests и результаты архивированы отдельно в `tb/reports/cp36`.

[Machine-readable evidence](verification-cp36.json) проверяет все raw hashes,
production inputs CP36f, actual context RTL CP36g и соответствие tested copies.
У CP36g изменена только metadata строка launcher, как описано выше.

Следующий gate должен учитывать **58 LUT / 26 slices** в production или
**37 LUT / 14 slices** с context hook. Этого пока недостаточно, чтобы обещать
полный MMU: APR lookup/CSR, MMR, PDR checks, PA22, abort/restart и extended
RK DMA ещё требуют измерения. Продолжать общий datapath и microcode approach;
не складывать изолированные probe estimates вместо полного HC1200 fit.
