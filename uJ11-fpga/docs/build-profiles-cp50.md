# CP50 — MMU-less по умолчанию, MMU сохранён под ifdef

Решение пользователя от 2026-09-11: остановить попытки разместить MMU на
HC1200. Даже неполный CP47c требует 1297 LUT / 351 FF / 7 EBR / 650 slices,
превышая 1280 LUT / 640 slices; защиты и restart в нём ещё нет. CP49
зафиксирован коммитом `c976628`. Исходники и результаты экспериментов сохранены.

## Выбор конфигурации

| | По умолчанию | Сохранённый эксперимент |
|---|---|---|
| Make | `MMU=0` или без параметра | `MMU=1` |
| Verilog | `UJ11_MMU` **не определён** | `UJ11_MMU` определён |
| Board bus | VA=PA, 16 бит | VA16 → PA22, выбор 18/22 |
| MMU sources | отсутствуют | 6 модулей |
| Основа RTL | CP40h | CP47c |
| CPU/FIS/microstore | общий, 954/1024×36 v12 | тот же |
| FRAM | нижний банк, прежний transport | оба банка, shared RX CP47 |

`MMU=0` означает отсутствие define, **не** `-DUJ11_MMU=0`: Verilog `ifdef`
проверяет присутствие имени. Make отвергает значения, отличные от `0`/`1`.
Флаг действует для `board`, `test-board-rt11` и `synthesis-board`;
изолированные исторические ISA/MMU tests имеют собственные настройки.

```sh
make board                         # MMU-less, стандартная сборка
make test-board-profiles            # обе конфигурации, без Diamond
make test-board-rt11                # native cold FB + DIR
make synthesis-board BOARD_CHECKPOINT=cp50a

make board MMU=1                    # только явное включение прототипа
make synthesis-board MMU=1 BOARD_CHECKPOINT=cp50b
```

Каждый новый synthesis требует свободного имени checkpoint. `export-board`
проверяет реальные MAP/PAR/TRACE и source hashes; прототип с MAP FAIL не
экспортируется для платы. Физически остаётся CP29a; CP50 плату не программирует.

`build/board-mmuless/` и `build/board-mmu/` содержат независимые `inputs.json`,
`simulation.f` и `synthesis.f`. Все пути в `.f` относительны корня `uJ11-fpga`.
Для Icarus/Verilator command files define записывается как `+define+UJ11_MMU`.
Обычные CLI drivers используют `-DUJ11_MMU`; Diamond —
`prj_impl option -impl impl1 VERILOG_DIRECTIVES {UJ11_MMU}`. Этот путь
подтверждён установленным `data/flow_script/syn_synplify.tcl`, который
переводит VERILOG_DIRECTIVES в Synplify `set_option -hdl_define -set`.

## Что разделено

Условные блоки находятся непосредственно в `uj11_board.v`,
`uj11_board_bus.v` и `uj11_board_fram.v`. Default ветвь не содержит
APR/PAR/PDR, MMR, translation bridge, PA22, MMU bypass или лишних тактов.
Состав MMU source list определён отдельно в `tools/board_common.py`.
Сгенерированный ранее shared APR перенесён в
`rtl/experimental/uj11_mmu_apr_shared.v` под тот же guard; сборка MMU больше
не зависит от наличия временных каталогов `build/cp39-*`…`build/cp47-*`.

Ядро, ALU, RF, PSW, decoder, ROM images и firmware не изменены. Улучшения
CP36/CP38/CP40 остаются в рабочем профиле. Специфичные для CP47 bus/FRAM
формы сохранены в MMU-ветви, чтобы не приписывать default их неизмеренную
стоимость. FPGA не получает run-time переключателя или mux для выбора профиля.

MMU-less CPU использует 16-битное пространство с I/O page. Верхние 64 КиБ
физической FRAM не отображаются; отсутствие MMU не выдаётся за доступ ко всем
128 КиБ. Сам transport по-прежнему тестируется с обеими banks.

## Проверки

- **96** сравнений RTL после Icarus preprocessing с source archives CP40h
  и CP47c: оба профиля, portable и `SYNTHESIS` ветви. Сравниваются токены,
  игнорируются только комментарии/пробельное форматирование. Сначала
  проверяются SHA256 архивных исходников. Это проверка сохранения RTL,
  а не новый gate-level proof или измерение Fmax.
- Списки `.f` обоих профилей успешно elaborated; default hierarchy содержит
  **0 MMU modules**, opt-in — 6. Synthesis source lists препроцессируются.
- Native bus: **30 beats** и **16 777 216** сочетаний адресов/overlay/RK
  состояния; bootstrap, UART, KW11, panel, SD, RK DMA/EBR/RTI.
- FRAM: 2 профиля × CLK_DIV=1/3, по 2048 transactions, всего **8192**;
  banks/byte/word/odd/held ACK и сравнение всей модели 128 КиБ.
- MMU CPU: portable 32 words × 8 pages × 18/22 bits — **558 684 clocks**,
  4880 PAR reads; vendor 4 words/page/mode — **97 692 clocks**, 848 PAR reads.
  В обоих проверены enable/disable lifetime, wrap18, NXM22, canonical I/O,
  MOVB sign/lane, odd vector4, mapped stack и high-bank opcode/immediate.
- Default cold RT-11FB + DIR: **354 938 300 clocks**, 3 984 366 retirements,
  5 217 011 reads, 423 616 writes, 3 390 712 FRAM transactions, 300 RK commands,
  576 timer edges, 3270 UART wire bytes, 162 SD reads / 6 RAM-overlay writes.
  Counts и UART совпали с CP40h. Backing image неизменён.

FIS/EIS не переписывались; совпадение всех CPU/ALU/microcode inputs
подтверждено. Полный старый ISA/FIS corpus в CP50 повторно не запускался.
Симуляции не заменяют проверку платы или физического timing.
[Manifest и raw logs](verification-cp50.json).

## Ресурсы и отложенная работа

Последние реальные измерения: **CP40h 1159 LUT / 326 FF / 6 EBR /
584 slices / 31.470 MHz**; **CP47c 1297 / 351 / 7 / 650**, MAP FAIL, Fmax нет.
Новые результаты CP50 пока отсутствуют. Архив для двух synthesis gates
содержит 43 файла, 248286 байт; список `/tmp/cp50-files.txt`. Передача на
`sash@192.168.1.108:/tmp/uj11-cp50.7DpErp` отклонена автоматической проверкой
и ожидает явного разрешения. Диски, ключи и `microasm11` в payload не входят.

CP47c — kernel unified PAR relocation, а не законченный J-11 MMU. Нет
PDR protection/length/W, MMR1/2, hardware fault metadata, abort250/restart,
modes/SP banks/I-D и high RK DMA. RT-11XM не загружалась. Эти функции,
FP11 и ODT остаются отложенными; новые MMU area experiments не планируются.

Исторические генераторы CP31–CP49 и их microcode сохранены для исследования.
Их exact-source guards привязаны к прежним исходникам: воспроизведение старого
checkpoint выполняется из его `source.tgz`/`test-sources.tgz` либо коммита
`c976628`, в отдельном checkout. Текущий поддерживаемый способ собрать
сохранённый прототип — `make board MMU=1`; старые отчёты не перезаписываются.
