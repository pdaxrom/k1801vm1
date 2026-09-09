# CP9: word unary и условные переходы

Исторический checkpoint; текущие byte forms и итоговые цифры — в [CP10](byte-instructions.md).

Поддержаны word `CLR COM INC DEC NEG ADC SBC TST ROR ROL ASR ASL` во всех
восьми addressing modes и 15 branch classes: `BR BNE BEQ BGE BLT BGT BLE
BPL BMI BHI BLOS BVC BVS BCC BCS` (`BHIS`/`BLO` — aliases BCC/BCS).
Это расширение Stage 1. Byte forms, SWAB/SXT/MARK, JMP/JSR/RTS/SOB,
architectural traps/IRQ и EIS пока отсутствуют. MMU отсутствует полностью.

## Дешёвый dispatch и существующий datapath

Unary encodings 005000..006377 octal распознаются двумя небольшими масками.
`entry = {00, IR[11:6], |IR[5:3], 0}`: mode0 идёт прямо в одну ALU
microinstruction по адресам 0a0,0a4,…,0cc. Все остальные modes идут на
entry+2: CALL общей DESTINATION_EA, затем JUMP в memory tail. Вложенного
CALL нет. Dynamic RD выбирает RF register без расширения 16×16 RF.

Branch index `{IR[15],IR[10:8]}` выбирает entry `040 | index<<2`.
Индекс 1..15 даёт 044..07c; index0 и прочие opcodes остаются STOP.
BR_DIRECT занимает одно слово. Остальные условия составлены из CJUMP
C/V/Z/N и их инверсий. BGE/BLT проверяют N и V последовательно;
BGT/BLE добавляют Z, BHI/BLOS — C и Z. Общие tails 18f..193 и BR taken
по 180 используют прежний sequencer. Новый boolean branch engine отсутствует.

RF/ALU/Q/IR/PSW/MDR/memory engine/sequencer не изменены относительно CP8.
Encoding остаётся **v4, 36 bits**, microstore — **342 words / 33.3984375%**.
Из 128 добавленных слов 86 относятся к unary (36 entries + 50 memory tails),
42 — к branch. Физически остаются четыре 1024×9 EBR.

## Результаты, flags и I/O

| ISA | Existing ALU / operands | PSW update |
|---|---|---|
| CLR | PASSA, D=ZERO | NZVC |
| COM | NOTA | NZVC; C=1 |
| INC / DEC | ADD / SUB, D=ONE | NZV; C сохраняется |
| NEG | SUB, D−A, D=ZERO | NZVC; C=borrow |
| ADC / SBC | ADC / SBC, A±0±C | NZVC |
| TST | PASSA, без writeback | NZVC; V=C=0 |
| ROR / ROL / ASR / ASL | ROR / ROL / ASR / LSL | NZVC; V=N xor C_out |

Register forms вычисляют результат, записывают RD и фиксируют flags в одном
execution cycle. С FETCH это 2 microclocks при zero-wait RAM. BR также 2;
conditional branches — 3..5, в зависимости от класса и выбранной ветви.
PSW при branch не изменяется.

Memory tails сначала вычисляют EA общим микрокодом. **CLR не читает конечный
destination**; это следует из существующего DCJ11 executor и предотвращает
лишние I/O read side effects. Deferred modes всё равно читают указатели.
TST выполняет только READ. Остальные unary выполняют ровно READ + WRITE.

Для write operations flags фиксируются только после успешного WRITE ACK.
Старый operand остаётся в MDR либо T0; финальная микрокоманда повторяет
ALU computation с прежним C. Отдельные temporary flags/FF не добавлены.
На failed WRITE память и PSW сохраняются; STOP — diagnostic fault, не
architectural trap. При ошибке после EA updates полная идентичность
частичного register state DCJ11 пока не заявляется.

## FRAM и цена speculation

У MR45V100A отменённый SPI READ должен закончить слово. Первый CP9 вариант
оставлял prefetch включённым на CJUMP и успевал начать лишнее слово до
решения о переходе. BNE self-loop стоил 144 CPI вместо 108 sequential-only.

Принятый микрокод задаёт `prefetch=0` на всех branch CJUMP. Последующие ALU
наследуют pause; следующий FETCH снова разрешает speculation. Retained READ
не отключён. Это изменение только микрокода, без новых LUT/FF в gate CP9e/f.
BNE self-loop стал 108 CPI. В not-taken BNE loop цена паузы — 42.09375
вместо 40.15625 CPI; у более длинных predicates потери больше. Countdown
`MOV #16,R1; DEC R1; BNE DEC; BR start` улучшился с 88.176471 до 72.352941 CPI.
Все варианты с clocks/CS/SCK: [benchmarks-cp9.json](benchmarks-cp9.json).

FRAM transport, byte lanes и I/O decode не менялись. I/O page
160000..177777 octal по-прежнему demand-only; применяются изученные правила
[периферии lsi11-fpga](fram-peripherals.md).

## Verification и gates

Oracle — реальный `core_step` из существующего `../core/`, model DCJ11,
с `ENABLE_MMU=0`, полным bus trace и детерминированной RAM. Общие callbacks
вынесены в `tb/trace_oracle.h`; прежний EA fixture остался побайтно тем же.

* Unary: 7104 candidates, 7080 завершённых, 24 abort исключены явно.
  Все 12×8 modes; register/indirect/absolute operand edges 0,1,2,7ffe,7fff,
  8000,8001,fffe,ffff со всеми 16 комбинациями NZVC; R6/R7 и wrap/indexed.
* Branch: 7440 завершённых без exclusions. Все 15×16 сочетаний class/flags,
  все 256 offset values у каждого class; отдельные PC bases 0,1000,dffe
  и смещения −128,−1,0,1,127. Это не exhaustive Cartesian 15×16×256.
* 46 directed unary tests: нормальные CSR обращения, ошибки чтения/записи,
  нечётный word address, стабильный request при 0..3 waits и отсутствие
  лишних side effects. Сохранены 14 double-operand error/I/O tests.
* Сохранены 31671 EA и 12928 RR/BR comparisons. Registers, full PSW,
  PC, memory writes и каждое demand обращение должны совпадать с oracle.

Полные portable/vendor результаты: [verification-cp9.json](verification-cp9.json).
No-wait/RAM-waits и SPI FRAM проверяются отдельно; functional simulation
не заменяет запуск платы. Failed/trapping candidates не объявлены совместимыми.

Итоговый HC1200 MAP/PAR/TRACE: **615 LUT4 / 277 FF / 4 EBR, 40 MHz PASS,
Fmax 40.925 MHz** для core+probe; **910 / 393 / 4, 29.56 MHz PASS,
Fmax 30.876 MHz** для core+FRAM/prefetch+probe. 370 LUT остаются до 1280,
но полная периферия и board pin timing ещё не входят в этот scope.
[Все checkpoints и critical paths](synthesis.md).
