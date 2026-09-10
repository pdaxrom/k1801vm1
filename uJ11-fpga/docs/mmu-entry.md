# CP35 — служебный вход в микрокод перед обращением к памяти

Проверен автоматический переход в отдельную routine перед каждым FETCH,
READ и WRITE с возвратом на исходную микрокоманду. Основной RF16×16,
Q и занятый EA CALL link сохранены. **Это механизм входа, ещё не MMU:**
routine не читает APR, не транслирует адрес и не формирует MMU abort.
Production остаётся CP31c, физическая плата — CP29a.

## Измерение на HC1200

| Revision / scope | LUT4 | FF | EBR | Slices | TRACE MHz | Words |
|---|---:|---:|---:|---:|---:|---:|
| [CP35a](../synth/reports/cp35a/result.json), controller probe | 23 | 42 | 0 | 22 | 150.150 | — |
| [CP35b](../synth/reports/cp35b/result.json), исходный CPU с ROM decoder | 690 | 303 | 5 | 346 | 34.136 | 954 |
| [CP35c](../synth/reports/cp35c/result.json), CPU + context hook | 732 | 315 | 5 | 366 | 35.854 | 963 |
| [CP35d](../synth/reports/cp35d/result.json), masked-OR redirect, отвергнут | 814 | 315 | 5 | 408 | 33.686 | 963 |
| [CP35e](../synth/reports/cp35e/result.json), полный board, enable=1 / hold=0 | 1273 | 338 | 6 | 638 | 30.498 | 963 |

Diamond 3.14.0.75.2, LCMXO2-1200HC-4SG32C, constraint 29.56 MHz:
все MAP/PAR/TRACE PASS, fully routed. External pin delays не заданы.
В controller probe 30 FF измерительные, 12 FF — состояние entry.
В одинаковых CPU probes b/c/d по 208 measurement FF. Их LUT и Fmax
нельзя подменять показателями полного board.

Полный CP31c занимал 1252 LUT / 326 FF / 6 EBR / 628 slices / 30.917 MHz.
CP35e добавляет **21 LUT, 12 FF и 10 slices** с прежней периферией;
остаётся только **7 LUT, 2 slices и 1 EBR**. Здесь enable постоянно включён,
hold постоянно снят: будущие MMR/APR controls могут увеличить стоимость.
Этот fit не доказывает размещение MMU и не принят как новая production сборка.

Вариант d заменял mux адреса на masked OR в controller и microsequencer.
Рост относительно c — 82 mapped LUT. Hierarchical Synplify ORCALUT4
в sequencer растёт 121→194, в entry 16→22; общий ORCALUT4 655→737.
Это локализует основной рост в sequencer, но primitive counts не следует
складывать с mapped LUT4. Сходный невыгодный широкий OR уже наблюдался
в [CP23](area-sequencer.md). Вернули c; d сохранён только в source snapshot.

## Контракт entry/return

`rtl/experimental/uj11_mmu_entry.v` содержит saved uPC10 и phase2:
IDLE → ROUTINE → RESUME → IDLE. Вход блокирует внешний запрос и сохраняет
uPC, затем synchronous microstore получает `MMU_ENTRY`. Основной CALL link
не используется. Saved uPC не требует reset: переход в ROUTINE всегда
предваряется его записью; phase сбрасывается явно.

`MMU_RETURN` возвращает исходный uPC. RESUME удерживается до **advance
возобновлённой микрокоманды**, включая fault continuation. ACK физического
FETCH недостаточно: synchronous opcode decoder требует ещё один такт.
Сброс RESUME на раннем ACK повторно вызывает entry перед тем же FETCH.

Во время ROUTINE внешний memory request закрыт. `hold_routine` останавливает
uPC/ROM и datapath/PSW updates; разрешён reset. Routine работает в word mode
независимо от byte opcode в IR. IRQ/trace/peripheral state следует обычной
логике CPU; новых trap stages нет. После возврата guest microinstruction
выполняется с обычными wait states и обработкой bus/address faults.

Только T5–T7 могут менять значение: их мёртвость на всех 88 memory words
доказана для текущего ROM в [CP34](mmu-sharing.md). R0–R7, T0–T4, Q, IR,
MDR и CALL link не записываются служебной routine. PSW16 сохраняется в T5,
NZVC намеренно изменяются и проверяются conditional branch, затем весь
PSW восстанавливается. MDR копируется в T7 без изменения самого MDR.

## Backend и microstore

`microasm/uj11entryasm.py` загружает отдельный экземпляр существующего v12
backend и добавляет только `MMU_RETURN` без аргументов. Это обычный JUMP
на STOP `$3ff` с reserved control bit1. Активный entry controller заменяет
адрес возврата; orphan return останавливается. Это экспериментальное
расширение, production encoding/backend и `microasm11` не изменены.

`microcode/mmu_entry.uasm` занимает 9 свободных слов в трёх дырках
`$1cd`, `$1dd`, `$32d`; выполняются 8 слов, одно STOP — защитная ветвь.
`tools/build_mmu_entry.py` проверяет overlaps, сохранность всех 954 исходных
слов и labels, затем создаёт ROM, listing, label map, occupancy и EBR image
в `build/cp35/`. Итого **963/1024×36**, 61 свободное слово, те же четыре
microstore EBR. Core/engine/seq адаптируются точными проверяемыми заменами
только в build-копиях; production RTL не меняется.

Измеренный overhead — **9 clocks на entry + один clock на каждую паузу**.
Это цена упражнения на сохранение состояния. Lookup, relocation, PDR
checks и MMR metadata пока отсутствуют; окончательная MMU routine и её CPI
будут измеряться отдельно.

## Проверки

* Unit controller: 210246 cycles, все 1024 return uPC, reset/enable/hold,
  незавершённые и возобновлённые memory words. 11551 вход, 10729 возвратов;
  разница — намеренные reset во время routine.
* CPU miter: исходный CPU пропускает clock edges только во время private
  routine кандидата. На обычных фронтах сравниваются bus requests/data,
  архитектурные регистры, живые temporaries, Q/IR/MDR, восстановленный PSW,
  CALL link и control state. Все 65536 opcode + 4096 seeded cases:
  69632 сценария, 2554664 обычных фронта, 390550 входов/возвратов,
  1244984 held edges, 4759934 дополнительных такта = 9×390550+1244984.
  Покрыты все 88 memory words, 4096 disabled и 34816 IRQ cases, wait0..3,
  bus faults. Дополнительно Icarus: 1024 сценария. Это динамическое
  сравнение, не exhaustive proof всех ISA состояний.
* Проверка pause делается после clock edge, включая T5–T7 и временный PSW.
  Пять отдельных build-only дефектов обнаружены: потеря PSW, порча CALL link,
  раннее снятие RESUME, запись во время hold, byte flags внутри routine.
  Assembler отвергает недопустимые поля, адрес вне ROM и overlap.
* FIS с неизменёнными ожиданиями существующего C oracle: 23840 cases /
  3072 injected faults на RAM и столько же на actual SPI FRAM transport
  с prefetch, с периодическими private holds. Guest bus здесь ещё 16 bits.
  Неподправленная Lattice DP8KC model: 645 cases / 83 faults в Icarus,
  каждый 37-й fixture исходного corpus, исходные case IDs сохранены.
* Strict Verilator `--Wall`: controller и обе CPU версии с portable ROM.
  Vendor microstore отдельно проверен Icarus и реальным Diamond fit.
* Полный холодный RT-11FB board regression использует обычный UART wire
  scoreboard, SPI FRAM/SD models, таймер, `DIR` и SD writeback overlay.
  406268404 clocks, 3986525 retirements, 3270 UART wire bytes,
  162 SD reads / 6 writes. Точные counts записаны в [manifest](verification-cp35.json).
  Исходный диск не изменяется. Это не RT-11XM/MMU проверка.

## Воспроизведение и следующий gate

```
make test-mmu-entry
make test-mmu-entry-fis          # требует build/vendor DP8KC/GSR/PUR
make test-mmu-entry-board       # cold RT-11FB, portable ROM, fixed-on hook
python3 tools/record_cp35.py     # проверить hashes и сохранить evidence
```

На Linux с Diamond: сначала `make mmu-entry`, затем `tools/checkpoint.py`
для cp35a/b/c и `tools/checkpoint_entry_board.py cp35e` в свежих build dirs.
Опубликованные отчёты неизменяемы. Для воспроизведения отвергнутого d
использовать его `source.tgz`: текущий generator уже возвращён к c.
У c текущий общий `tools/checkpoint.py` отличается добавленной конфигурацией d;
HDL, microcode и generator совпадают с frozen inputs. Все исходные версии
launcher, input hashes и raw reports сохранены в synthesis archives.

Следующий gate — **сократить площадь полного board перед интеграцией MMU**.
Одного оставшегося EBR достаточно для APR store, но семь LUT не являются
запасом для MMR, PA22, PDR checks, abort/restart и управления lookup.
После area gate: kernel unified translation, MMR/restart, physical DMA и
CPU доступ ко всем 128 КиБ; затем обязательный RT-11XM из пользовательского
`../lsi11/disks/rt11v5.3/system.dsk`. XM пока не загружен, FPGA не прошивалась.
