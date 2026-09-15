# CP78: PSW 177776 внутри CPU

CP78 — отдельный аппаратный кандидат над замороженной CP67b. В нём
оригинальные DFFPA/DFFPB/DFFPC проходят без тестового адаптера PSW.
На плате остаётся CP67b. **CP78a/b/c не прошли MAP**: 649/643/648 slices при пределе 640.
Синтез выполнен 2026-09-15; PAR/TRACE и Fmax отсутствуют.
Это функциональный результат симуляции, не готовая замена установленного JED.

## Документированная семантика

Основание: DEC **DCJ11 Microprocessor User's Guide**, October 1983,
§1.3, Figure 1-3, Table 1-3 (pp. 1-3…1-7).
[Локальный оригинал](../../doc/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf),
[DEC PDF на Bitsavers](https://www.bitsavers.org/pdf/dec/pdp11/1173/EK-DCJ11-UG-PRE_J11ug_Oct83.pdf).
Наш C emulator `core/core.c`, `dcj11_explicit_psw_write` и flag macros
использован для дополнительной сверки поведения явных записей.

- `177776` — слово PSW и его младший байт; `177777` — старший байт.
  Данные байтового core interface остаются выровненными вправо.
- Чтение возвращает PSW до изменения флагов читающей командой.
  Например, `MOV @#177776,R0` переносит старые NZVC, а затем формирует
  обычные MOV N/Z/V, сохраняя C.
- Явная запись загружает NZVC из результата записи. Более позднее обновление
  флагов той же инструкции не перезаписывает их. Это распространяется
  на MOV, арифметику, shifts и многошаговый SWAB.
- Явная запись сохраняет T (`PS<4>`). RTI/RTT, trap и service context load
  продолжают загружать T своим обычным путём.
- `PS<10:9>` всегда нули, включая полную загрузку PSW через RTI/START.
  Это исправление CP78: CP67 могла хранить произвольные значения этих битов.
  Зарезервированный `PS<8>` сохраняет документированную возможность R/W.
- При byte write меняется только выбранный байт с указанной защитой T.
  Word access по нечётному `177777` вызывает обычный address fault.
- Изменение IPL учитывается на границе завершения инструкции. Trace по
  прежнему T имеет приоритет над отложенным IRQ.

Профиль остаётся с одним регистровым набором и без MMU. Mode/RS bits —
сохраняемые метаданные существующего профиля; переключения R0–R5/SP и
полноценной защиты processor modes это изменение **не добавляет**.

## Где реализовано

[build_psw_cp78.py](../tools/build_psw_cp78.py) проверяет SHA256 входов
`synth/reports/cp67b/source.tgz`, восстанавливает их в `build/cp78-psw`,
подставляет [core](../rtl/cp78/uj11_core.v) и [PSW](../rtl/cp78/uj11_psw.v)
и добавляет в engine фиксацию явной записи до конца инструкции.
Архив CP67b и его генератор не меняются.

Декодирование PSW стоит в core перед внешним memory interface. Внутреннее
обращение завершается без внешнего request; неверный внешний error при этом
не влияет на CPU. Odd-word проверка остаётся в `uj11_mem`. Синхронный opcode
ROM видит тот же внутренний read data/ack, что и engine. Оба интерфейса
`ALIGNED_WORD_READS=0/1` и `ROM_DECODE=0/1` сохраняются.

Логические обращения ACTIVE/GUEST видят текущий PSW процессора. В HALT это
**живой HALT PSW**, а сохранённый USER CPSW по-прежнему читается через RCPS
или служебную запись контекста. UPPER/LOWER (`mem_physical=1`) обходят PSW:
физические слова `0FFFE` и `1FFFE` остаются FRAM. RK DMA не проходит через
CPU-local decode. Для FP11-команд с операндом по адресу PSW остаётся отдельная
задача firmware: перенаправить такой доступ к сохранённому USER CPSW.
Само добавление CPU-local PSW не делает HALT-контекст равным USER-контексту;
полную совместимость этих FP/MMIO alias случаев CP78 не заявляет.
Изменений board bus, SPI FRAM, периферии и EBR нет в исходниках.

Microstore побайтно совпадает с CP67b: **1005/1024 слов, 36 бит**, 19 свободных.
Новых микротактов нет. Цена дополнительной логики измерена: CP78a — 1290 LUT, CP78b — 1277,
CP78c — 1286; все три превышают доступные slices.
[Полные synthesis results](synthesis.md). CP78b прошёл 131078 тактовых
сравнений битовых data/control mux с CP78a, включая неиспользуемые X-входы;
мутация защиты T отвергнута. Это исчерпывающая проверка побитовых mux,
не формальное доказательство всего CPU. PSW regression CP78b: 1741/12486;
FP events: 111/4174. У CP78c есть только synthesis result, без functional PASS.

## Проверки

[test_psw_cp78.py](../tools/test_psw_cp78.py) запускает настоящие инструкции
через reset/vector/START или RTI/RTT. Стенд задаёт только внешнюю память,
не подменяет RF, PSW или внутреннее состояние CPU. ACK задерживается на 0–3
такта. Проверяются оба банка, байтовые полосы, T, carry-зависимые инструкции,
SWAB, приоритет destination flags, следующий INC, trace/IRQ stack frame,
odd/unmapped faults и физический доступ к последнему слову обеих половин FRAM.

| Прогон | Результат |
|---|---|
| PSW, synchronous decode, aligned word interface | 1741 cases / 12486 checks |
| PSW, combinational decode, right-justified bytes | 1741 cases / 12486 checks |
| PSW, Lattice DP8KC simulation | 1741 cases / 12486 checks; те же clocks, что portable |
| FP11 IRQ/fault/trace/debug, portable | 111 cases / 4174 checks |
| FP11 IRQ/fault/trace/debug, vendor | 111 cases / 4174 checks |
| Полная RT-11/SPI FRAM/SD/UJMOD/ODT/FP11 | 99 checks / 832980518 clocks / 9776 UART bytes — PASS |

FP event fixtures CP76/77 сохраняются; два ожидания `PSW=FF00|NZVC`
заменены на `F900|NZVC` согласно документированным нулевым PS<10:9>.
Полный board test использует настоящие модели SPI FRAM/SD/UART и частную
копию RT-11. Только literal-счётчики ожидания ESC (`RLOOPS`, `RSPINS`)
сокращены до 1, как в CP77; производственная длительность этого окна
проверялась отдельно в CP67 и здесь заново не квалифицируется.
UART отличается от CP77 только датой файла FPTST.SAV в DIR: 14→15 Sep 2026.
Весь сценарий занял на 67930 clocks больше CP77 (832912588), включая
OS polling, загрузку и холодные старты. Это не изолированное измерение
стоимости PSW или CPI отдельной команды.

Числовой FP engine и ODT CP77 не менялись. Два остальных сочетания
`ROM_DECODE/ALIGNED_WORD_READS` также прошли по 1741 cases / 12486 checks.
Счётчики всего набора: 228164 clocks с logic decode, 238577 с synchronous;
в них входят reset и подготовка случаев, это не CPI инструкции.

[run_dec_fp_cp78.py](../tools/run_dec_fp_cp78.py) использует извлечённые
неизменённые DEC absolute-loader images с проверенными checksums из CP77.
У стенда **нет** PSW read adapter. Внешние условия: zero-wait RAM,
KL11 TX ready/RX empty, console switches=0; прочее внешнее I/O вызывает error.

| Оригинальная диагностика | Проходы | Ошибки | Core clocks |
|---|---:|---:|---:|
| FFPAA1.BIN / DFFPA | 1 | 0 | 177182640 |
| FFPBA0.BIN / DFFPB | 1 | 0 | 2669489 |
| FFPCB0.BIC / DFFPC | 1 | 0 | 1476999 |

Clocks совпали с CP77 с его test-only adapter; на CP67b без адаптера DFFPA
останавливалась в test 2 на `MOV @#177776,R3` по `004706`, trap PC `004712`.
Теперь эту операцию выполняет RTL CPU. Эти clocks не являются FRAM speed
benchmark. Заводские M8267-TA abort проверки, требующие специального
оборудования, этими тремя проходами не квалифицированы.

Архив: [manifest](../tb/reports/cp78/archive.json), точные исходники и логи рядом.
`verify_cp78.py --current` сверяет их SHA256 с текущими файлами, не запускает
synthesis и не объявляет FPGA gate пройденным.

## Воспроизведение

Из корня репозитория, с зависимостями [CP67](development-cp67.md):

```sh
python3 uJ11-fpga/tools/build_psw_cp78.py
python3 uJ11-fpga/tools/test_psw_cp78.py
python3 uJ11-fpga/tools/test_psw_cp78.py --decode 0 --aligned 0
python3 uJ11-fpga/tools/test_psw_cp78.py --vendor
python3 uJ11-fpga/tools/test_fp_events_cp78.py
python3 uJ11-fpga/tools/test_fp_events_cp78.py --vendor
python3 uJ11-fpga/tools/prepare_dec_fp_cp77.py
python3 uJ11-fpga/tools/run_dec_fp_cp78.py FFPAA1.BIN
python3 uJ11-fpga/tools/run_dec_fp_cp78.py FFPBA0.BIN
python3 uJ11-fpga/tools/run_dec_fp_cp78.py FFPCB0.BIC
python3 uJ11-fpga/tools/run_fp11_rt11_cp78.py --out /absolute/path/uJ11-fpga/build/cp78-rt11-new
```

FP event/DEC tests используют готовые `build/cp77-fp11/test-image.mem`,
`fp76_symbols.vh` и `software/image.bin`. Для их восстановления сначала
выполнить сборку и тестовые приготовления CP77 по [инструкции](odt-fp-cp77.md).
Число 76 в имени symbol include — имя интерфейса прежнего testbench.
Новый полный RT-11 прогон требует свежего каталога и собственной копии диска.

На Linux synthesis host, после разрешённой передачи:

```sh
python3 uJ11-fpga/tools/checkpoint_board.py cp78a --psw-cp78 --clock-mhz 31.824 --fram-timing
```

LCMXO2-1200HC-4SG32C, OSCH nominal 29.56 MHz, те же FRAM budgets и timing
constraint 31.824 MHz, что CP67b. До успешного routed gate и анализа резерва
CP78 не принимается в production и не прошивается.
