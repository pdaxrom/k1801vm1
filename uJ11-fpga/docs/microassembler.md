# Микроассемблер uJ11

Это руководство по [uj11asm.py](../microasm/uj11asm.py): как написать
микропрограмму, собрать ROM и проверить изменение. Побитовая разметка дана
отдельно в [формате микрокода](microcode-format.md). Описание соответствует
кодированию v12 и текущему [execution engine](../rtl/uj11_engine.v).

Микроассемблер собирает управляющие слова CPU шириной 36 бит.
Обычные программы PDP-11 — bootstrap, ODT, программный FPP — собираются
MACRO/LINK из `firmware/`; для них см. [разработку](development.md).
`microasm11` в этой цепочке не используется.

## Быстрая сборка

Все команды выполняются из `uJ11-fpga`. Для отдельной сборки микрокода
достаточно Python 3 со стандартной библиотекой:

```sh
python3 microasm/uj11asm.py microcode/uj11.uasm \
  -o build/microcode/uj11.mem \
  --list build/microcode/uj11.lst \
  --labels build/microcode/uj11.labels.json \
  --stats build/microcode/uj11.stats.json
```

Текущая программа выводит:

```text
uJ11: 1005/1024 microinstructions, 36 bits, 98.14% occupied
```

| Аргумент | Содержание |
|---|---|
| `source` | Один текстовый исходник; обязательный позиционный аргумент |
| `-o PATH`, `--output PATH` | Обязательный путь к ROM image |
| `--list PATH` | Listing явно размещённых микрокоманд |
| `--labels PATH` | JSON: имя метки → полный микроадрес |
| `--stats PATH` | JSON: занятость ROM и число слов между метками |
| `-h`, `--help` | Справка по командной строке |

Каталоги результатов создаются автоматически. Указанные выходные файлы
перезаписываются. Ошибка сборки даёт ненулевой exit code и сообщение в stderr.

Для создания Verilog с четырьмя EBR lanes из этого образа:

```sh
python3 tools/make_ebr.py build/microcode/uj11.mem build/microcode/uj11_rom.v
```

Для рабочего компьютера использовать `make hardware`: он собирает единый
[uj11.uasm](../microcode/uj11.uasm), dispatch и firmware ROM вместе.
Его результаты находятся в `build/hardware/`. Самостоятельная команда выше
удобна для проверки синтаксиса и не подменяет ROM штатной сборки.
После изменения аппаратного микрокода для платы нужны synthesis и новая
прошивка FPGA; загрузчик модулей UJMOD не записывает microstore.

## Текстовый синтаксис

Одна микрокоманда занимает одну строку и одно слово ROM. Поля разделяются
запятыми; запись поля — `имя=значение`. У ALU название операции отделяется
пробелом: `alu ADD, ...`. У control-команды операции ALU нет: `JUMP, ...`.
Перенос продолжения команды на следующую строку не поддерживается.

Комментарии начинаются с `;`. Пустые строки игнорируются. Регистр букв
не важен для мнемоник, полей, enum-значений и меток.
Метка имеет вид `[A-Za-z_][A-Za-z_0-9]*:` и может стоять перед командой
на той же строке. В JSON имена меток записываются прописными буквами.

| Запись | Значение |
|---|---|
| `32` | Десятичное 32 |
| `$20`, `0x20` | Шестнадцатеричное 20, то есть десятичное 32 |
| `0o40` | Восьмеричное 40, то есть десятичное 32 |
| `0b100000` | Двоичное 100000, то есть десятичное 32 |
| `FETCH` | Адрес метки FETCH |

**Числа по умолчанию десятичные.** Запись PDP-11 вида `000340` не является
восьмеричным литералом этого assembler: нужно `0o340`. Восемь бит `imm`
содержат беззнаковое значение 0..255; `imm=-1` запрещён, `imm=$ff` даёт
16-битное `00ff`, а не `ffff`.

Единственная директива — `.org ADDRESS`, которая меняет текущий адрес.
Начальный адрес равен нулю. Можно размещать участки не по порядку, но нельзя
записывать два слова по одному адресу. Диапазон микроадресов — 0..1023,
или `$000`..`$3ff`. Метку перед `.org` лучше не ставить: она связывается
с адресом **до** выполнения директивы.

Ссылки на метки вперёд поддерживаются в командах: сначала собираются
метки, затем кодируются слова. В самой `.org` разрешены число или уже
объявленная метка. Арифметических выражений вроде `LABEL+1`, `.equ`,
`.include`, макросов, условной сборки и линкера отдельных секций нет.
Значения enum нужно писать именами: `a=R1`, а не `a=1`.

## ALU: поля и значения по умолчанию

```text
alu OP, a=R0, b=R0, pair=AB, dst=NONE, flags=KEEP, d=ZERO, seq=NEXT
```

В этой записи все поля, кроме `OP`, необязательны. Отсутствие `dst` означает
отсутствие записи, а не запись в `b` по умолчанию.

| Поле | Допустимые значения | По умолчанию |
|---|---|---|
| `OP` после `alu` | PASSA, PASSB, ADD, ADC, SUB, SBC, AND, OR, XOR, BIC, NOTA, LSL, LSR, ASR, ROL, ROR | Обязательно |
| `a`, `b` | R0..R7, T0..T7, RS, RD, RS1, RD1 | R0 |
| `pair` | AB, AQ, AD, DB, ZB, DQ, ZQ, DA, BA | AB |
| `dst` | NONE, RF, Q, RFQ_L, RFQ_R, OPERAND, MOV | NONE |
| `flags` | KEEP, NZV, NZVC, LOAD | KEEP |
| `d` | ZERO, ONE, TWO, STEP, MDR, DISP, IMM, PSW, STATUS | ZERO |
| `seq` | NEXT, PAGE, FETCH, FETCH_A1 | NEXT |
| `next` | Полный микроадрес или метка в той же странице | Только при PAGE; обязательно |
| `imm` | Число или метка с численным значением 0..255 | Только при IMM; обязательно |
| `trace` | RETURN | Отсутствует |

### Выбор регистров и пары операндов

RF состоит из R0..R7 и T0..T7. R6 — SP, R7 — PC. Порт `a` читает один
регистр, порт `b` читает второй и одновременно задаёт **адрес записи RF**.
Нельзя независимо выбрать третий регистр назначения в одном слове.
`dst=RF` выбирает способ записи, а не номер регистра.

`RS` выбирает R[IR[8:6]], `RD` — R[IR[2:0]]. `RS1`/`RD1` делают OR номера
с единицей: R2→R3, R3→R3. Это полезно для пар EIS и не означает увеличение
номера на один. У однорегистровой инструкции значение RS определяется её
реальным форматом opcode, поэтому его нельзя автоматически считать source.

Пусть A и B — прочитанные значения RF, D — выбранный вход `d`, Q — Q-register.
`pair` задаёт именно входы ALU:

| `pair` | Первый вход L | Второй вход R |
|---|---|---|
| AB | A | B |
| AQ | A | Q |
| AD | A | D |
| DB | D | B |
| ZB | 0 | B |
| DQ | D | Q |
| ZQ | 0 | Q; допустимо только с `d=ZERO` |
| DA | D | A |
| BA | B | A |

`PASSA` означает «передать L», `PASSB` — «передать R» после выбора пары.
Поэтому загрузка константы пишется как
`alu PASSA, pair=DA, d=IMM, imm=7, b=T0, dst=RF`.
Одно `d=IMM` при `pair=AB` лишь кодирует константу: ALU её не использует.

### Операции, C и флаги

| Операции | Результат |
|---|---|
| PASSA, PASSB | L, R |
| ADD, ADC | L+R, L+R+C |
| SUB, SBC | L−R, L−R−C |
| AND, OR, XOR | L AND R, L OR R, L XOR R |
| BIC, NOTA | L AND NOT R, NOT L |
| LSL, LSR, ASR | Сдвиг L на один бит; ASR сохраняет знак |
| ROL, ROR | Сдвиг L на один бит через старый C |

C берётся из PSW[0]. При сложении новый C — перенос, при вычитании — заём.
У PDP-11 `CMP src,dst` вычисляет src−dst, а `SUB src,dst` записывает dst−src.
Поэтому текущий CMP_RR использует `pair=AB`, SUB_RR — `pair=BA` при одинаковых
`a=RS, b=RD`. Для BIC_RR также нужна BA: результат dst AND NOT src.

`KEEP` сохраняет PSW. `NZV` меняет N/Z/V, сохраняя C; `NZVC` меняет все
четыре флага; `LOAD` загружает **весь PSW** из ALU result. PASS и логические
операции дают V=C=0, кроме NOTA: она даёт C=1. У сдвигов C — вытесненный бит,
V=N XOR C. NZV/NZVC не затрагивают T, IPL и старшие биты PSW.

### Запись результата, byte и Q

| `dst` | Действие |
|---|---|
| NONE | RF и Q не меняются; обновление flags возможно |
| RF | RF[B]←result, полное слово |
| Q | Q←result |
| OPERAND | Word: RF[B]←result; byte: старший байт RF[B] сохранён |
| MOV | Word: RF[B]←result; byte: знаковое расширение result[7:0] |
| RFQ_L | RF[B]←{result[14:0],Q[15]}, Q←{Q[14:0],0} |
| RFQ_R | RF[B]←{result[15],result[15:1]}, Q←{result[0],Q[15:1]} |

В ALU-слове нет поля `byte`. Engine выбирает byte arithmetic по IR, если
заданы `flags=NZV/NZVC` **или** `dst=OPERAND/MOV`. Поэтому обычные расчёты
адресов с `dst=RF, flags=KEEP` остаются 16-битными даже во время MOVB.
Напротив, лишнее `flags=NZV` у такого расчёта включит byte flags для byte opcode.
`dst=RF` сам по себе всегда пишет все 16 бит; для архитектурной byte-записи
используются OPERAND/MOV.

При RFQ_L/RFQ_R флаги вычисляются по ALU result **до** дополнительного
RF/Q shift. Эти режимы не дают автоматически флаги 32-битной операции.
Обычные ALU-сдвиги используют L, не Q; для работы с Q выбирается нужная пара
и способ записи. Barrel shifter, hardware MUL/DIV и отдельного loop counter нет.

### Вход D

| `d` | Значение |
|---|---|
| ZERO, ONE, TWO | 0, 1, 2 |
| STEP | 1 для byte opcode и выбранного A=R0..R5; иначе 2 |
| MDR | Последнее принятое слово/байт памяти; IRQ также использует MDR для вектора |
| DISP | При IR[14]=0: знаковый IR[7:0]×2; при IR[14]=1: беззнаковый IR[5:0]×2 |
| IMM | Беззнаковое расширение поля `imm` до 16 бит |
| PSW | Текущий PSW целиком |
| STATUS | Состояние служб USER/HALT и отладчика |

STEP выбирается по **порту A**, а не B. Modes 2/4 используют STEP;
deferred modes 3/5 и продвижение PC после displacement всегда требуют TWO.
STATUS допустим только с NEXT/FETCH, без `trace`; он использует контекстный
бит 7, поэтому несовместим с PAGE/FETCH_A1/IMM.

### Завершение ALU-микрокоманды

| `seq` | Следующий адрес |
|---|---|
| NEXT | uPC+1 по модулю 1024 |
| PAGE | Верхние два бита текущего uPC и младшие восемь бит `next` |
| FETCH | Граница инструкции, затем обычный FETCH либо pending debug/trace/IRQ |
| FETCH_A1 | То же завершение, только если исходное RF[A]==1; иначе NEXT |

PAGE требует цель в той же 256-словной странице. Аргумент `next` всё равно
задаётся **полным** адресом: из `$160` к `$170` — `next=$170`, не `$70`.
Для другой страницы нужен отдельный JUMP. PAGE и IMM делят low8 и потому
несовместимы; IMM разрешён с NEXT, FETCH и FETCH_A1.

FETCH_A1 проверяет все 16 бит A **до записи результата**, независимо от Z.
Так SOB может одновременно уменьшить счётчик и завершиться, если было 1.
`trace=RETURN` — специальное завершение RTI/RTT: нужны `seq=FETCH`,
`flags=KEEP`, вход D не IMM/STATUS. Это не обычная команда возврата CALL.

## Control-команды

У всех control-команд разрешено `prefetch=0/1`, по умолчанию 1.
В текущей плате speculative prefetch отсутствует; поле оставлено в формате.
У ALU-слов `prefetch` отсутствует. Для READ stream-семантика также не означает,
что в этой сборке существует отдельный prefetch buffer.

Звёздочка в таблице означает обязательное поле. Остальные поля брать только
из соответствующей строки: например, `FETCH, a=R7` assembler отвергает.

| Команда | Поля кроме `prefetch` | Действие |
|---|---|---|
| JUMP | `target*`, `a`, `init`, `service` | Переход по полному микроадресу |
| CJUMP | `target*`, `cond*` | По условию target, иначе uPC+1 |
| CALL | `target*` | link←uPC+1, переход target |
| RETURN | Нет | Переход по сохранённому link |
| FETCH | Нет | Чтение слова по PC, IR/MDR←слово, PC←PC+2, opcode dispatch |
| DISPATCH | Нет | Opcode dispatch без чтения памяти и изменения PC |
| OR_MS, OR_MD, OR_RR, OR_BT | `target*` | Переход target OR биты IR/условий |
| OR_R67 | `target*`, `a` | Переход target OR признак группы регистра A |
| READ | `target*`, `a`, `b`, `byte`, `space`, `stream`, `fault_inc` | Чтение по RF[A] в MDR, затем target |
| WRITE | `target*`, `a`, `b`, `byte`, `space` | Запись RF[B] по адресу RF[A], затем target |
| TRAP | `target*` | Переход к микрокоду frame entry; включает защиту построения frame |
| STOP | Нет | Остановка engine |
| WAIT | Нет | Ожидание debug/trace/IRQ |

По умолчанию A/B=R0, `byte=0`, `space=ACTIVE`, `stream=0`, `fault_inc=0`,
`init=0`, `service=NONE`. У FETCH assembler сам кодирует A=B=R7 и
ADD/AD/RF/TWO; менять эти поля нельзя. Поле `b` у READ принимается, но
результат чтения идёт в MDR, а не RF[B].

**`seq=FETCH` и control `FETCH` различаются.** Первое завершает текущую
инструкцию и допускает обработку событий; второе получает новый opcode.
`JUMP, target=FETCH` просто меняет uPC и не заменяет retirement.
На плате синхронный decoder добавляет внутренний такт после приёма opcode;
память всё это время не должна получать повторную транзакцию.

CALL имеет один link register. Повторный CALL до RETURN и RETURN без CALL
переводят sequencer на `$3ff`. Подпрограммы должны сохранять нужные вызывающей
стороне T-регистры, Q и PSW самостоятельно. JUMP из подпрограммы не очищает
link. Макроса вложенных вызовов или скрытого стека assembler не создаёт.

У TRAP `target` — **микроадрес**, не адрес PDP-11 vector. Например, BPT
предварительно помещает `0o14` в T4, затем выполняет TRAP к TRAP_ENTRY;
уже эта routine читает vector и пишет stack frame.

### Условия CJUMP

Допустимы ALWAYS, C, V, Z, N, Q0, LOOPZ, ERROR и каждое с префиксом
`NOT_`, включая NOT_ALWAYS. Читаются текущие флаги предыдущей ALU-команды,
Q0 — текущий Q[0]. NOT_ALWAYS всегда ложен.

В production engine входы LOOPZ и ERROR подключены к нулю: положительные
условия всегда ложны, отрицательные всегда истинны. Счётчик держат в RF
и проверяют через Z. Ошибки памяти обрабатываются redirect-логикой, а не
`CJUMP, cond=ERROR`. Сам sequencer primitive допускает внешние значения
этих входов, что используется его unit test.

### OR dispatch

| Команда | Добавляемые биты | Выравнивание target |
|---|---|---|
| OR_MS | IR[11:9] | 8 слов |
| OR_MD | IR[5:3] | 8 слов |
| OR_RR | bit0: source mode=0, bit1: destination mode=0 | 4 слова |
| OR_BT | bit0: byte opcode, bit1: RF[A][0] | 4 слова |
| OR_R67 | bit0: NOT(A_index[2] AND A_index[1]) | 2 слова |

У OR_BT синтаксис не допускает `a=`: в текущем кодировании A=R0.
Следовательно, его нечётность относится к R0, а не к автоматически выбранному
EA/MDR. У OR_R67 можно указать `a=RS` или `a=RD`: R6/R7 дадут смещение 0,
R0..R5 — 1. Для T-регистров применяется та же битовая формула без трактовки SP/PC.

Assembler требует, чтобы **все** адреса target..target+mask были явно
собраны, даже если часть входов для данного opcode недостижима.
Недостижимые позиции можно заполнить явным STOP; автоматически вставленные
STOP в незанятой ROM не считаются размещёнными dispatch entries.

### Память, byte и fault_inc

READ/WRITE не совмещают запись RF или обновление PSW с транзакцией.
MDR захватывается после успешного чтения; его перенос в RF занимает следующее
ALU-слово. Address/data/uPC удерживаются до ACK. Byte data передаётся в
младших восьми битах, а bit0 адреса сохраняется для выбора lane на плате.

`byte=0` — слово, `byte=1` — принудительный байт, `byte=IR` — ширина текущего
opcode. `stream=1` разрешён только у READ с A=R7/RS/RD и `byte=0/IR`.
При `byte=1` он запрещён. READ сам не увеличивает PC или autoincrement-регистр.

Нечётное word-обращение даёт fault без bus request; byte по нечётному адресу
допустим. При ошибке чтения IR/MDR/RF/PSW не получают результат неудачной
транзакции. Обычный USER fault направляется на `$015`, HALT fault — на `$2b6`;
ошибка во время защищённого построения frame останавливает CPU.

`fault_inc=1` — исключение для архитектурного autoincrement после неудачного
READ. Перед fault entry исполняется ровно одна continuation по `target`.
Assembler требует, чтобы она была:

```text
alu ADD, a=тот_же_селектор, b=тот_же_селектор, pair=AD, d=STEP_или_TWO, dst=RF
```

У неё обязательны KEEP/NEXT без low8-расширений и иных побочных действий.
После успешного READ эта же continuation выполняется обычным образом.
Нельзя переносить туда захват MDR, запись Q или обновление flags.

### USER/HALT-службы

`space=ACTIVE` выбирает текущий USER/HALT банк с обычными overlays;
GUEST — логический USER банк; UPPER/LOWER — физический верхний/нижний банк
FRAM без I/O/ROM overlays. Все special spaces требуют word и non-stream.
CPC/CPSW — обычные слова в UPPER, отдельных `space=CPC/CPSW` нет.

У `JUMP, service=ENTER/LEAVE/CONFIG` нужны `prefetch=0, init=0`.
ENTER переключает engine в HALT, LEAVE — в USER. Сохранение/восстановление
PC и PSW выполняют окружающие микрокоманды. LEAVE также взаимодействует
с STEP/WAIT по IR и состоянию debugger, поэтому используется штатными
служебными routines, а не как универсальный bank switch.

CONFIG читает RF[A]. STATUS можно получить ALU-командой с `d=STATUS`:

| Бит | STATUS | CONFIG |
|---|---|---|
| 0 | HALT/ODT ready | Записывает ready |
| 1 | FPP ready | Записывает ready |
| 2 | Debug enabled | Записывает |
| 3 | Debug pending | Записывает |
| 4 | Debug context | Не записывает |
| 5 | Возврат в WAIT | Записывает |
| 6 | Сохранённый trace | Записывает |
| 7 | NOT FPP ready | Не записывает |
| 8 | 1 | Не записывает |
| 15:9 | 0 | Не записывает |

Поле `init=1` разрешено только у JUMP с `prefetch=0`, без service:
это импульс сброса периферии, а не reset CPU/RF/FRAM.
Карта контекста и модульный протокол описаны в [архитектуре](architecture.md)
и [ABI модулей](modules.md).

## Проверяемые примеры

Каждый блок `uasm` ниже — отдельный исходник, который assembler принимает
целиком. Сохранить блок в `build/microcode/example.uasm` и передать его как
`source` команде из начала руководства. Это учебные фрагменты ROM;
инициализация RF, memory model и подача IR для исполнения требуют стенда.
Их нельзя прошивать вместо полной программы `uj11.uasm`.

### Добавление register-register инструкции: BIS

BIS уже реализован; на нём можно проследить весь путь без введения нового
opcode. Для `BIS R1,R2` ожидается R2←R2 OR R1, N/Z по результату, V=0,
C сохранён. Например, R1=`0o10`, R2=`0o3` дают R2=`0o13`.

1. По [декодеру](../rtl/uj11_decode.v) определить entry. Double-operand
   class 5 при обоих mode=0 даёт `$150`; source/destination номера остаются в IR.
2. В этом entry разместить ALU-слово с RS/RD, OR, OPERAND и NZV.
   `dst=OPERAND` позволяет той же routine корректно выполнять BISB.
3. Завершить его `seq=FETCH`, чтобы сохранить границу инструкции и события.

```uasm
.org $000
RESET:
    JUMP, target=FETCH
.org $020
FETCH:
    FETCH
.org $150
BIS_RR:
    alu OR, a=RS, b=RD, pair=AB, dst=OPERAND, flags=NZV, seq=FETCH
.org $3ff
UNSUPPORTED:
    STOP
```

Получаются четыре явно размещённых слова. Наличие метки BIS_RR само по себе
не меняет opcode decoder: адрес связывается с opcode в RTL, не по имени метки.
Одна execution microinstruction не означает один такт всей PDP-11 инструкции:
fetch, синхронный decode и SPI wait clocks учитываются отдельно.

4. Для memory modes использовать уже существующие SOURCE_EA, DESTINATION_EA
   и CAPTURE_REGISTER_SOURCE. У текущего BIS_EA entry `$158`; T0 содержит
   source, T1 — destination EA, T2 — destination/result. Source mode 0
   захватывается **после** обновлений destination EA, как принято в этом ядре.
5. Через OR_MD разделить destination register и memory. В memory пути
   сначала READ, затем ALU в T2, затем WRITE; архитектурные flags фиксировать
   после успешного WRITE. Иначе fault может оставить неправильный PSW.
   Полные routines BIS_EA/BIS_MEMORY находятся в рабочем `uj11.uasm`.
6. Если opcode действительно новый, изменить `uj11_decode.v`, проверить
   qualifier `byte_instruction` и service dispatch в engine. Нынешняя
   dispatch EBR хранит только **9 бит entry**, поэтому прямой opcode entry
   должен быть ниже `$200`; из него можно прыгнуть в любую страницу microstore.
   Генератор [build_decode_rom.py](../tools/build_decode_rom.py) перебирает
   все 65536 opcodes и отвергает несовместимые строки сжатой таблицы.
7. Проверить оба исходных C, нулевой/отрицательный результат, byte high-byte
   preservation, совпадающие Rs/Rd, PC и memory faults. Для полноценного ISA
   изменения нужны instruction/RTL regression и новое измерение synthesis.

Не выбирать адрес по «пустому месту» в тексте: `.org` идут не по порядку.
В рабочем ROM занято 1005 слов, свободные 19 разбросаны; занятые STOP тоже
участвуют в dispatch и не являются автоматически доступным резервом.

### CALL и цикл на временном регистре

```uasm
.org $000
    alu PASSA, pair=DA, d=IMM, imm=3, b=T0, dst=RF
    CALL, target=COUNT_DOWN
    STOP
.org $100
COUNT_DOWN:
    alu SUB, a=T0, b=T0, pair=AD, d=ONE, dst=RF, flags=NZV
    CJUMP, cond=NOT_Z, target=COUNT_DOWN
    RETURN
```

Шесть слов. При word-контексте IR цикл уменьшает T0 от 3 до 0.
Он меняет NZV; вызывающая routine должна учитывать это. Возврат идёт к STOP
сразу после CALL. Переход CJUMP не создаёт вложенный вызов.

### Таблица destination modes

```uasm
.org $000
    OR_MD, target=MODE_TABLE
.org $100
MODE_TABLE:
    JUMP, target=REGISTER
    JUMP, target=MEMORY
    JUMP, target=MEMORY
    JUMP, target=MEMORY
    JUMP, target=MEMORY
    JUMP, target=MEMORY
    JUMP, target=MEMORY
    JUMP, target=MEMORY
.org $110
REGISTER:
    STOP
MEMORY:
    STOP
```

Одиннадцать слов; таблица занимает `$100`..`$107`. Вход 0 выделен отдельно,
остальные семь ведут к общей routine. Удаление даже одного table word даст
`control flow to unassembled address`.

### READ с проверяемым autoincrement

```uasm
.org $000
    READ, a=RS, byte=IR, stream=1, fault_inc=1, target=INCREMENT, prefetch=0
INCREMENT:
    alu ADD, a=RS, b=RS, pair=AD, d=STEP, dst=RF
    alu PASSA, pair=DA, d=MDR, b=T0, dst=RF
    STOP
```

Четыре слова. После успешного чтения инкрементируется RS, затем значение
копируется из MDR. При fault захват MDR пропускается; для исполнения такого
случая стенд должен содержать штатные fault entries.

## Listing, labels и статистика

ROM image содержит всегда 1024 строки по девять hex-цифр. Строка n —
36-битное слово по микроадресу n. Незаполненные позиции содержат STOP.
Это текст для `$readmemh`/генератора EBR, не массив PDP-11 little-endian слов.

Listing отсортирован по микроадресам; колонки:

```text
uaddress_hex  word_hex  source_line_decimal  исходная команда
```

Комментарии в listing не сохраняются, номер относится к исходному файлу.
JSON labels содержит численные десятичные значения адресов.

В stats есть `encoding_version`, `word_bits`, `physical_words`, `used_words`,
`occupancy_percent`, `highest_address` и `routines`.
`used_words` считает явно собранные инструкции, включая явные STOP.
`highest_address` не равен размеру программы: STOP на `$3ff` делает его 1023
даже в четырёхсловном примере.

`routines.NAME.words_until_next_label` — число размещённых слов от метки
до следующей по адресу. Это не анализ CALL graph и не динамический CPI;
внутренние метки дробят одну routine на несколько участков. Alias-метки
с одинаковым адресом тоже не дают независимых размеров подпрограмм.
Микротакты и memory cycles измеряются стендами, см. [benchmarks](benchmarks.md).

## Диагностика и границы проверки

Большинство ошибок выводится как `source: line N: причина`. Проверка
`fault_inc` после кодирования сообщает микроадрес READ.

| Сообщение или его начало | Причина и исправление |
|---|---|
| `expected field=value` | Пропущен `=`; указывать поля через запятые |
| `unknown field` | Поле не существует или запрещено у данной команды |
| `duplicate/conflicting field` | Одно поле задано дважды, даже одинаковыми значениями |
| `empty field` | После `=` нет значения |
| `invalid value ... expected ...` | Неизвестная мнемоника/enum; допустимые имена перечислены в сообщении |
| `unknown symbol or number` | Неизвестная метка, выражение или неверная запись числа |
| `invalid or duplicate label` | Неверное имя или повторная метка |
| `overlap at ...` | `.org` привела к адресу с уже размещённым словом |
| `... outside 10-bit range` | Микроадрес вне 0..1023 |
| `imm outside 8-bit range` | Литерал вне 0..255 |
| `... outside 1-bit range` | Числовой флаг должен быть 0 или 1 |
| `PAGE needs next and conflicts with IMM` | Не задан next либо low8 одновременно занят константой |
| `PAGE target crosses page` | Для другой страницы использовать JUMP |
| `next field requires seq=PAGE` | У NEXT/FETCH/FETCH_A1 поля next нет |
| `d=IMM requires imm` / `imm requires d=IMM` | Источник и значение константы должны задаваться вместе |
| `... target overlaps OR bits` | Base таблицы не выровнен на 2/4/8 слов |
| `control flow to unassembled address` | Переход/продолжение/dispatch ведёт в неразмещённое слово |
| `... requires target` / `CJUMP requires cond` | Не указано обязательное поле |
| `ZQ requires d=ZERO` | Для произвольного D+Q использовать pair=DQ |
| `STATUS conflicts ...` / `trace=RETURN requires ...` | Конфликт контекстных low8-полей |
| `stream READ requires ...` | Неверный A или принудительный byte для stream |
| `special space requires ...` | GUEST/UPPER/LOWER требуют word/non-stream |
| `service requires ...` / `init requires ...` | Проверить service/init/prefetch |
| `fault_inc at ... needs ...` | Continuation должна быть строго ADD того же регистра с STEP/TWO |
| `empty microcode` | Не размещено ни одной команды |

Assembler проверяет статические ALU NEXT/PAGE/FETCH-продолжения, target,
ветвь fall-through CJUMP, возвратный адрес CALL и все OR entries.
Автоматическое заполнение пустот STOP не разрешает статические переходы в них.

Он **не доказывает** корректность ISA/flags, сохранность временных регистров,
максимальную динамическую глубину CALL, соответствие opcode decoder меткам
и покрытие неявных hardware entries (reset, IRQ, trace, fault, debug).
DISPATCH/FETCH и RETURN имеют динамические цели; самостоятельный маленький
образ может собраться, хотя ещё не является работоспособным CPU ROM.

## Проверка изменения в рабочем проекте

Для синтаксиса, кодирования и EBR packing достаточно:

```sh
python3 -m unittest discover -s tests -p 'test_microasm.py' -q
```

Для ALU/RF/Q/PSW/memory/sequencer primitives — `make test-rtl`.
После изменения рабочей микропрограммы собрать `make hardware`, посмотреть
listing/labels/stats и провести подходящие [RTL-проверки](verification.md).
Общие entry/dispatch/flags затрагивают также EIS/FIS, traps и USER/HALT-службы.

`make verify` сравнивает результат с сохранённым аппаратным baseline;
ожидаемо выявит намеренное изменение ROM. Не обновлять эталонные hashes
просто ради зелёного результата: новое поведение подтверждается тестами,
synthesis и отдельным релизом. Поскольку `make test` начинается с verify,
при разработке новой версии нужные тестовые targets запускаются отдельно.
Полный порядок и зависимости приведены в [development.md](development.md).
