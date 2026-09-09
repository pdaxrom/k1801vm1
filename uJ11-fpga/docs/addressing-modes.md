# CP7: word addressing modes — историческая база

CP8 расширил эту базу до семи word operations. В текущем CP10 добавлена
[byte ISA](byte-instructions.md); цифры и ограничения ниже относятся к CP7.

Реализованы все восемь source и destination modes для **word MOV, CMP, ADD**.
BR и прежние RR fast entries сохранены. Это не завершение всей Stage 1:
byte ISA, остальные ALU instructions, условные branches, JMP/JSR/RTS/SOB
ещё предстоит реализовать. Адреса остаются 16-bit, MMU отсутствует.

## Организация без hardware EA engine

RF16×16, ALU, Q, IR/MDR/PSW и microsequencer не расширялись. Decoder принимает
opcode classes 1/2/6 и непосредственно образует адрес `{01,opcode,!rr,000}`:
110/120/160 для RR, 118/128/168 для EA. Каждый slow entry вызывает SOURCE_EA,
DESTINATION_EA и позднее считывание регистрового source, затем OR_MD выбирает
register/memory execution routine. Вложенных CALL нет; прежнего link достаточно.

| Регистр | Назначение |
|---|---|
| T0 | Сохранённое значение source |
| T1 | Эффективный адрес destination |
| T2 | Старый destination, затем ADD result |
| T3 | Временный EA/pointer; в ADD memory — сохранённый старый destination |

`OR_MS` и `OR_MD` непосредственно добавляют IR mode bits к адресу таблицы.
Увеличивать RF или добавлять новый dispatch operation не потребовалось.
Весь активный microcode — **143/1024 words**, включая 23 исходных M0 слова.
Имя `microcode/m0.uasm` сохранено как историческое; текущий файл включает CP7.
Точный прежний M0 воспроизводится из архивов synthesis, а не текущего image.

| Mode | Source routine | Destination EA routine |
|---|---|---|
| 0 Rn | Отложить считывание до конца destination EA | Регистровый operand |
| 1 (Rn) | Читать слово по Rn | Копировать Rn в T1 |
| 2 (Rn)+ | Читать, Rn+=2 | Сохранить Rn в T1, Rn+=2 |
| 3 @(Rn)+ | Читать pointer, Rn+=2, читать operand | Читать pointer, Rn+=2, pointer→T1 |
| 4 -(Rn) | Rn-=2, читать operand | Rn-=2, сохранить EA |
| 5 @-(Rn) | Rn-=2, читать pointer, читать operand | Rn-=2, читать pointer→T1 |
| 6 X(Rn) | Читать extension по PC, PC+=2, Rn+extension, читать operand | Читать extension, PC+=2, Rn+extension→T1 |
| 7 @X(Rn) | Как 6, затем дополнительное pointer read | Как 6, затем pointer→T1 |

Rn при вычислении indexed EA считывается **после** PC increment: это важно
для PC-relative. Сложение/вычитание естественно оборачивается в 16 bits.
Immediate/absolute используют PC modes 2/3. Для word все increments равны 2,
включая SP/PC; byte +1/+2 rules пока не объявляются реализованными.

У DCJ11 `MOV R0,(R0)+` сохраняет уже увеличенный R0. Такой source mode 0
считывается после destination EA, как в существующем `core/core.c` для DCJ11.
Memory source сохраняется до изменения destination registers. Общие routines
проверены также при совпадающих Rs/Rd, SP/PC и отрицательных extensions.

MOV destination EA **не читает destination**. CMP выполняет только read,
ADD — read и write. ADD сохраняет старый destination в T3 и повторно вычисляет
NZVC после успешного WRITE ACK; при ошибке записи новый PSW не фиксируется.
Это один дополнительный ALU расчёт без дополнительных registers/flags latch.

## FRAM и микрокодная политика prefetch

Control bit5 остаётся stream hint. `READ, a=RS/RD, stream=1` разрешает
stream только если выбранный register действительно R7. Это маленький
`uj11_stream`, а не вычислитель EA. Operand `(R0)+` не становится потоком PC.
Новый bit4, `prefetch=0`, запрещает новые speculative launches. Последний
control policy наследуется ALU words через один FF; immediate/page bits ALU
не заняты. Demand accesses, retained SPI READ и уже начатый transfer продолжают
работать. FETCH по умолчанию разрешает prefetch.

EA control words держат pause, пока впереди могут быть data reads/writes.
RETURN для destination mode 0 открывает безопасное окно после source read.
Так устранена измеренная регрессия: непрерывно разрешённый prefetch начинал
следующее слово, затем отменял его при доступе к operand, тратя лишние SPI
clocks. На `MOV memory→register` CPI было 255.21875, стало 221.3125.
На immediate→register pause несколько ограничивает перекрытие: 77.9375 →
85.6875, но legacy FRAM даёт 223.25, sequential-only — 91.5. Этот tradeoff
сохранён явно; следующая оптимизация не должна снова ухудшать memory loops.

Цена в MAP относительно unrestricted CP7: **825→861 LUT4, 392→393 FF**,
4 EBR в обоих случаях. Итоговый FRAM target 29.56 MHz проходит; TRACE Fmax
30.658 MHz. Core + probe отдельно: 583 LUT4/277 FF/4 EBR, 40 MHz проходит,
Fmax 41.810 MHz. Полная периферия платы в эти counts не входит.

## Проверки и границы совместимости

`tb/ea_vectors.c` вызывает **существующий DCJ11 core_step** с ENABLE_MMU=0.
Его callbacks регистрируют каждое чтение/запись; ROM fixture не содержит
копии ISA implementation. Матрица перебирает 3 instructions × 8 source modes
× 8 destination modes × 8 Rs × 8 Rd, плюс memory/immediate flag edges и wrap.
Из 13,842 кандидатов **13,587** завершаются без trap/I/O; **255** trap/I/O
fixtures исключены явно. Их нельзя считать успешно пройденной trap emulation.
Все 3×8×8 mode pairs присутствуют среди завершённых cases.

Testbench сравнивает R0..R7, полный PSW, число, порядок, адрес, направление
и данные **56,057 memory beats**. Для RAM используются 0..3 waits на beat;
второй вариант исполняет те же fixtures через SPI FRAM и prefetch. Отдельно
сохранены исходные 6272 RR/BR differential cases. Directed tests проверяют
MOV/CMP/ADD I/O side effects, odd source/pointer и write errors. До Stage 2
ошибки заканчиваются diagnostic STOP, без trap frame/vector fetch. Частичное
состояние регистров при abort не заявляется идентичным J‑11: source mode 2/3
увеличивает register после успешного READ ACK. Это нужно пересмотреть при
архитектурной обработке bus/address errors и restart.

Повторный запуск: `make test-ea`, `make vendor-ea`, `make benchmark-ea`,
`make vendor-ea-benchmark`. Полная регрессия — `make test`.
