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
keep buffer не должен входить в конечный netlist. Итоговая LUT-реализация
проверена synthesis ниже. `syn_use_carry_chain` также
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
`tb/reports/cp53`. Этот архив фиксирует проверки до synthesis; новый
resource gate и последующие проверки выбранного варианта записаны отдельно
в [synthesis-cp53.json](synthesis-cp53.json) и `tb/reports/cp53-final`.

## Synthesis

После явного разрешения пользователя переданы 11 файлов, 66157 байт,
на `sash@192.168.1.108:/tmp/uj11-cp53-20260911`. Остальные исходники
скопированы из проверенного CP52 archive на сервере. Все SHA256 до и после
сборок совпали с согласованными manifests и локально протестированным RTL.

Diamond 3.14.0.75.2 / Synplify V-2023.09L-2, LCMXO2-1200HC-4SG32C,
полный CPU/FIS/FRAM/KL11/KW11/panel/HG/SD/RK/firmware/OSCH/pins,
constraint 29,56 MHz. **Все три MAP/PAR/TRACE PASS**, полностью разведены.

| Gate | LUT4 | FF | EBR | Slices | Fmax, MHz | Slack, ns |
|---|---:|---:|---:|---:|---:|---:|
| CP52a, native baseline | 1159 | 326 | 6 | 584 | 31,470 | 2,053 |
| CP52b, sequential baseline | 1198 | 341 | 6 | 603 | 30,044 | 0,544 |
| CP53a, increment | **1195** | 341 | 6 | 602 | **31,338** | **1,919** |
| CP53b, compare | 1204 | 341 | 6 | 606 | 30,865 | 1,430 |
| CP53c, both | 1208 | 341 | 6 | 608 | 31,788 | 2,371 |

**CP53a выбран для дальнейшей оптимизации sequential FRAM:** −3 LUT,
−1 slice, +1,294 MHz относительно CP52b при тех же clocks/instruction.
CP53b/c дороже на 6/10 LUT, поэтому не выбраны. Даже полный отказ от
carry в cursor не гарантирует меньшую площадь всего компьютера.

У CP53a остаются **85 LUT / 38 slices / 1 EBR**, свободных PIO sites нет.
Цена ускорения против native baseline — +36 LUT/+15 FF. Цель <=1100 LUT
пока не достигнута, поэтому default остаётся CP52a; CPU/FIS/microcode,
MMU-ветвь и физическая плата CP29a сохранены. Microstore — 954/1024 слова.

### Что подтвердили netlist и TRACE

| FRAM mapping | ORCALUT4 | PFUMX | CCU2D |
|---|---:|---:|---:|
| CP52b | 95 | 9 | 13 |
| CP53a | 107 | 9 | 5 |
| CP53b | 104 | 9 | 8 |
| CP53c | 118 | 9 | 0 |

Это counts ячеек Synplify внутри FRAM; они не складываются напрямую
в итоговый MAP LUT4. В CP53a удалены восемь CCU2D инкремента, пять
ячеек сравнения сохранены. В B удалена цепь сравнения, в C — обе цепи.
Проверены 3025/3059/3073 сети конечных EDIF: сильных multiple drivers нет.
EDIF сохранены в `synth/reports/cp53*/design.edi.gz`.

Предупреждения не скрыты: набор кодов и число сообщений Synplify совпали
с CP52a/b (в том числе 100 выведенных BN161). MAP: три предупреждения,
ноль ошибок; остаются прежние сообщения о JTAG/GPIO, отключённых
configuration ports и local timer reset. Synplify также сообщает об
OSCH/inferred clock; итоговый TRACE использует явный LPF 29,56 MHz.
Внешние pin delays не заданы, поэтому внутренний Fmax не является
подтверждением timing SPI на физической плате.

Худший путь CP53a: EBR lane 2 → dynamic RF/address → board decode/ACK →
bus-fault predicate → microsequencer → EBR lane 1. **31,936 ns, 17 уровней,
58,9% routing**, slack 1,919 ns. Cursor в этот путь не входит; рост Fmax
нельзя приписать только сокращению задержки инкремента. Следующее
исследование — стоимость и глубина board decode/ACK, с сохранением
ROM/CSR/RK priority и отсутствием дополнительных memory clocks.

### Проверки выбранного CP53a после synthesis

- Девять новых full-board workloads с неизменёнными моделями Lattice
  DP8KC/GSR/PUR в Icarus. Все counters точно совпали с portable CP53a.
- Новый cold RT-11FB + DIR в Verilator: **288686609 clocks**, 300 RK
  commands, 467 timer edges, 3270 UART wire bytes, 162 SD reads/6 writes.
  Retired 3979364, reads 5207588, writes 422214, FRAM transactions 2422032.
  Каждый принятый FRAM beat проверен по модели памяти; UART wire,
  SD writeback и IRQ assertions прошли.
- Все cold counters и **raw UART bytes** точно совпали с CP52b.
  Против обычного native baseline сохранено ускорение 1,229×; reg-reg
  loops — 40,0625 CPI вместо 107 (2,671×).
- Backing image `lsi11-fpga/images/rt11v503.dsk`, 27540480 bytes,
  SHA256 `e769228f2e1262220297bfa98b8f2841688849ab4c49ad9cd48d0d73d0a99553`
  не изменён. Это MMU-less RT-11FB; RT-11XM здесь не проверялся.

Архивы новых tests, исходников и manifests: `tb/reports/cp53-final`.
Raw synthesis reports и frozen sources: `synth/reports/cp53a/b/c`.

Воспроизведение локальных проверок:

```sh
python3 tools/build_cursor_cp53.py
python3 tools/check_cursor_cp53.py
python3 tools/run_cursor_cp53.py
python3 tools/audit_edif_cp53.py
python3 tools/record_cursor_cp53.py
```

EDIF для audit находится в test-sources snapshot (`build/cp53-netlist`).
Для повторного synthesis нужны свежие имена gates; выполненные команды:

```sh
python3 tools/checkpoint_board.py cp53a --cursor-cp53 increment
python3 tools/checkpoint_board.py cp53b --cursor-cp53 compare
python3 tools/checkpoint_board.py cp53c --cursor-cp53 both
python3 tools/run_cursor_vendor_cp53.py increment
python3 tools/run_board.py --tag cp53-final --cursor-cp53 increment
python3 tools/record_synthesis_cp53.py
```

До запуска Diamond доступен `--prepare-only`. Варианты несовместимы с
`--mmu` и `--fram-cp52`, и не включены в default.
