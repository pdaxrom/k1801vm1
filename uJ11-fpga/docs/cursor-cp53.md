# CP53 — стоимость инкремента и сравнения FRAM cursor

Три небольших варианта сформированы из проверенного source archive CP52b.
Они сохраняют все 15 bits `next_word`, точную последовательность SPI, границы
READ и число CPU clocks. Default MMU-less RTL, FIS, microcode и сохранённая
MMU-ветвь не изменены. Плата остаётся CP29a.

## Что измеряется

| Gate | Вариант | Изменение |
|---|---|---|
| CP53a | increment | Binary +1 выражен как XOR каждого бита с AND младших битов |
| CP53b | compare | Пять сравнений по три бита, затем AND; промежуточные nets сохранены |
| CP53c | both | Оба изменения вместе |

У increment `advanced_word[0] = ~address[1]`, остальные биты равны
`address[i+1] XOR AND(address[i:1])`. Representation и overflow не меняются.
Группировка comparator проверяет все 15 bits; совпадение младших разрядов
никогда не подменяет полный адрес. Никаких page aliases/добавочных промахов нет.

В compare использован `/* synthesis syn_keep=1 */` на векторе wire.
Применимость к MachXO2 и типу Net проверена в установленном **Synplify Pro
for Lattice Attribute Reference, September 2024**, раздел `syn_keep`,
стр. 125–130. Директива сохраняет границы nets при оптимизации; временный
keep buffer не должен входить в конечный netlist. Итоговую LUT-реализацию
всё равно требуется подтвердить synthesis. `syn_use_carry_chain` также
изучен по стр. 264–268, но глобальные ограничения carry не применялись:
они затронули бы CPU ALU и периферию за пределами данного эксперимента.

## Исследование CP52 netlist и предупреждений

Получены реальные mapped EDIF из завершённых CP52a/b на сервере.
У FRAM в CP52b ровно **13 CCU2D**: восемь `un2_next_word_cry_*` относятся
к инкременту; пять `un1_address_2_0_I_*` — к цепи сравнения. Это уточняет
прежнюю оценку по aggregate MAP, которая не разделяла оба оператора.

В Synplify `.srr` есть BN161 о multiple drivers на промежуточных nets,
включая `un2_next_word_axb_*`; подобные сообщения были и у CP52a.
Эти новые имена отсутствуют в конечном EDIF. `tools/audit_edif_cp53.py`
проверяет направления портов каждой ячейки/instance и драйверы каждой сети:
**2982 nets CP52a, 3004 nets CP52b, ни одной сети с двумя сильными драйверами**.
Намеренно добавленный второй FF output успешно отвергается. Bidirectional
nets выделены отдельно; это структурный netlist check, не физическая проверка
состояний внешних устройств на двунаправленных выводах. Предупреждения
сохранены в raw reports и не замалчиваются как отсутствующие.

## Проверки до synthesis

- Шесть положительных sequential SAT proofs: три варианта, CLK_DIV=1/3.
  Сравниваются все совпадающие state bits и выходы, включая busy rdata,
  ready/error и SPI pins. Входы request/address/data/MISO не ограничиваются
  held-request контрактом. Это two-state proof; четыре логических состояния
  HDL не выдаются за область SAT.
- Три negative controls с потерянным переносом между младшим и старшим
  фрагментами cursor отвергаются. Ошибочный carry не может пройти за счёт
  совпадения приватных имён nets.
- 12288 случайных операций FRAM, оба банка, byte/word/odd/held request,
  полное сравнение 128 КиБ модели; 1962 направленные операции с границами,
  последовательными словами, reset и монитором CS/SCK timing.
- 129 board beats: bootstrap/vector/CSR boundaries, aligned byte reads,
  UART/SD side effects, private RK read/write и service release/reset.
- 27 full-board warm workloads: все counters каждого из трёх вариантов
  совпали с CP52. MOV/ADD/CMP R,R по-прежнему **40,0625 CPI**; 4224 SCK
  на 256 инструкций, BR self 107 CPI, memory workloads без изменений.

[Manifest и доказательства](verification-cp53.json), raw logs/source snapshot —
`tb/reports/cp53`. CPU/FIS/firmware не менялись; полные cold RT-11 и vendor
EBR проверки нового выбранного варианта выполняются после resource gate.
Прежний CP52 cold результат не объявляется новым прогоном CP53.

## Synthesis

На этом этапе **новых LUT/FF/EBR/Fmax ещё нет**. Для сравнения используются
измеренные CP52a (1159/326/6/31,470 MHz) и CP52b (1198/341/6/30,044 MHz).
Все три новых проекта подготовлены для LCMXO2-1200HC-4SG32C, 29,56 MHz,
с полным CPU/FIS/FRAM/KL11/KW11/panel/HG/SD/RK/firmware/OSCH/pins.

Автоматическая проверка отклонила передачу нового CP53 payload, сочтя
согласие CP52 ограниченным его файлами и путём. Запрошено отдельное
подтверждение: **11 файлов, 66157 байт**, список `/tmp/cp53-files.txt`,
сервер `sash@192.168.1.108`, каталог `/tmp/uj11-cp53-20260911`.
Payload содержит только изменённые RTL/build files и manifests; остальные
проверенные CP52 inputs уже находятся на сервере. Никаких дисковых образов,
ключей, `microasm11` и новой MMU-функциональности.

Воспроизведение локальных проверок:

```sh
python3 tools/build_cursor_cp53.py
python3 tools/check_cursor_cp53.py
python3 tools/run_cursor_cp53.py
python3 tools/audit_edif_cp53.py
python3 tools/record_cursor_cp53.py
```

EDIF для audit находится в test-sources snapshot (`build/cp53-netlist`).
Для synthesis после разрешённой передачи:

```sh
python3 tools/checkpoint_board.py cp53a --cursor-cp53 increment
python3 tools/checkpoint_board.py cp53b --cursor-cp53 compare
python3 tools/checkpoint_board.py cp53c --cursor-cp53 both
```

До запуска Diamond доступен `--prepare-only`. Варианты несовместимы с
`--mmu` и `--fram-cp52`, и не включены в default.
