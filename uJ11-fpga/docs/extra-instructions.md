# CP12: SWAB, SXT и MARK

Три word instruction classes дополняют CP11 без изменений RF/Q/ALU,
PSW, microsequencer, memory transport или 36-bit encoding v7. Добавлены
38 microinstructions, всего 393/1024. Все 355 прежних words и label addresses
сохранены. MMU, alternate register bank и дополнительные address bits отсутствуют.

## Семантика из существующих исходников

Первичный oracle — [`../core/core.c`](../../core/core.c), model DCJ11,
`ENABLE_MMU=0`. Структура addressing modes также сопоставлена с AM4
[`mc.asm`](../../lsi11-fpga/ucode/experimental/am4/mc.asm).

* SWAB меняет местами bytes word operand. N/Z относятся только к low byte
  результата, V/C сбрасываются. Memory form читает и пишет один word;
  register form заменяет весь word. Все восемь addressing modes.
* SXT пишет FFFF при исходном N=1, иначе 0000. N сохраняется, Z=!N,
  V=0, C сохраняется. Конечный memory operand не читается; indirect pointer
  и displacement, если они нужны, читаются обычным EA microcode.
* MARK: SP=PC+2×unsigned IR[5:0], PC=R5, R5=(SP), SP+=2. Здесь PC уже
  указывает за instruction word. Все 64 offsets; PSW не меняется.
  Конечный target может быть нечётным: fault возникает при следующем FETCH.

PSW фиксируется после успешного SWAB/SXT WRITE ACK. При MARK pop error
core оставляет SP на адресе неудачного чтения, PC уже принимает прежний R5,
сам R5 не перезаписывается. Это явно проверенное поведение данной реализации;
полная идентичность partial-abort state и stack-limit traps DCJ11 не заявлена.

## Микрокод

Entries: MARK 1a0; SXT reg/memory 1b0/1b4; SWAB reg/memory 1c0/1c8.
Общая SWAB routine 2d0 загружает Q из T0, очищает T2 и восемь раз выполняет
имеющийся RFQ_L. После OR с Q получен swapped word в T2. Дополнительного
ALU operation, byte-swap mux, barrel shifter или counter нет.

SWAB flags tail 1d0 маскирует исходный word по FF00. Word ALU N/Z на этом
значении равны N/Z младшего byte swapped результата; AND сбрасывает V/C.
В частности, исходный 0080 даёт результат 8000, но N=0, Z=1. Flags не берутся
из старшего byte результата. SXT использует CJUMP N и общий MOV memory tail.
MARK использует D=DISP: у этого opcode IR[14]=0 и IR[7:6]=0, поэтому branch
формула даёт требуемый unsigned six-bit offset. CALL routines не вложены.

## Реальные HC1200 gates

| Variant | Scope | LUT4 | FF | EBR | Constraint | TRACE Fmax MHz |
|---|---|---:|---:|---:|---|---:|
| cp12a | Priority decoder, core + probe | 708 | 277 | 4 | 35 MHz PASS | 37.012 |
| cp12b | Priority decoder, FRAM + probe | 1081 | 394 | 4 | 29.56 MHz PASS | 30.650 |
| cp12c | Parallel masks, core + probe | 708 | 277 | 4 | 35 MHz PASS | 35.723 |
| cp12d | Parallel masks, FRAM + probe | 984 | 394 | 4 | 29.56 MHz PASS | 31.287 |

CP12b отвергнут по стоимости: +111 LUT относительно CP11f. Рост появился
уже в Synplify (ORCALUT4 853→964), до routing. Замена приоритетной цепочки
на parallel OR непересекающихся class masks дала 984 LUT, то есть −97 LUT
в FRAM scope при неизменной функции decoder. Изолированный core LUT count
не изменился; результат зависит от оптимизации полного scope, а не только
числа строк decoder. Exhaustive test проверяет все 65536 encodings против
независимых octal masks; 55744 поддержаны. Огромной opcode ROM/PLM нет.

Принятый CP12c/d: +14 LUT в обоих scopes относительно CP11, без новых FF/EBR.
Остаются 296 LUT и 3 EBR у FRAM probe; полная board периферия и pin timing
ещё не входят в gate. 50 MHz не достигнуты. Все четыре input/source/report
архива сохранены в `synth/reports/cp12*`.

## Проверки и измерения

Extra suite: 5568 кандидатов DCJ11, 4524 завершённых (1468 SWAB/SXT,
3056 MARK), 1044 явно исключённых. Причины могут пересекаться: abort=4,
trap=4, I/O=1040, odd-word=0. Эти случаи не объявляются совместимыми.
Address и vector-entry audit добавлены только в build copy исходного emulator.
Четыре abort/trap — SWAB/SXT @-(PC): pointer читается из собственного
instruction word и оказывается нечётным. Отдельные directed tests проверяют
fault до внешнего odd-word request, сохранение PSW и остановку bus.
Их diagnostic outcome не приравнивается к ещё не реализованному vector 004.
[Exclusion audit](../tb/reports/cp12-extra-exclusion-audit.log).

Проверяются все modes/registers, 12 operand edges с 16 NZVC combinations
для register/indirect/absolute, indexed wrap, MARK offsets 0..63 и SP/PC
boundary addresses. Это заданные наборы покрытия, не полный Cartesian
product всех возможных состояний. Registers/PSW/PC и ordered bus trace
сравниваются после instruction: 9762 bus beats, 58385 clocks у RAM с
0..3 waits и 1161566 clocks у настоящего SPI FRAM transport/model.

36 directed cases проверяют CSR reads/writes, held requests, odd words,
failed read/write ACK, SWAB low-byte flags, SXT no-read и MARK pop effects.
Benchmark: 7 workloads × 4 memory modes. SWAB/SXT loops содержат 31 operation
и BR; 32 warmup и 256 measured instructions. MARK loop состоит из четырёх
instructions, включая установку R5 и восстановление SP; его CPI относится
ко всему loop. [Таблицы](benchmarks.md), [JSON](benchmarks-cp12.json),
[verification manifest](verification-cp12.json).

`make test-extra test-extra-faults benchmark-extra` и соответствующие
`vendor-extra vendor-extra-faults vendor-extra-benchmark` воспроизводят
новые suites. Full regression включает все прежние instructions и FRAM
benchmarks; recorder требует побайтного совпадения старых fixtures и cycle CSV.


Полный воспроизводимый запуск: `make verify-cp12`. Он сохраняет portable
CSV/JSON до vendor runs, прерывается при ошибке команды и вызывает recorder
только после всех проверок. Vendor models задаются через `LATTICE_SIM_DIR`;
если переменная не задана и есть `build/vendor/DP8KC.v`, используется эта
локальная копия. `python3 tools/verify_cp12.py --dry-run` показывает шаги.
Не запускать одновременно с другим verification в том же build directory.


В `uj11_engine.v` сохранён исторический комментарий CP11 возле D=DISP,
упоминающий branch/SOB. В CP12 поле также использует MARK с IR[7:6]=0;
аппаратная формула не менялась. Это уточнение относится к комментарию,
не к различию RTL между проверенным source archive и рабочим деревом.
