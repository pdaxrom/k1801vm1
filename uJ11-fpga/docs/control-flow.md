# CP11: JMP, JSR, RTS и SOB

Адреса строго 16-bit, MMU отсутствует. JMP/JSR modes 1..7 используют
существующий DESTINATION_EA и T1, без чтения конечного operand. Mode 0
недопустим и пока даёт diagnostic STOP; architectural traps — Stage 2.

DCJ11 oracle: `../core/core.c`, decode_data, JMP/JSR/RTS/SOB, pullw.
AM4 reference: `../lsi11-fpga/ucode/experimental/am4/mc.asm`, routines
0x0d8 SOB, 0x0f8 JMP, 0x130 JSR, 0x138 RTS. AM4 сохраняет/восстанавливает
PSW вокруг SOB decrement. Первый CP11 baseline использовал тот же приём.

## Порядок действий и aliases

JMP вычисляет EA и записывает PC. JSR вычисляет EA, уменьшает SP на 2,
пишет текущий link register по SP, затем link←PC, PC←EA. Поэтому JSR R6
кладёт уже уменьшенный SP; EA aliases и изменения PC учитываются до push.
Failed WRITE ACK не фиксирует link/target; PSW не меняется.

RTS сначала PC←Rn, затем word READ по SP, SP←SP+2, Rn←MDR. RTS R6
заменяет увеличенный SP снятым словом. RTS PC — обычный return из стека.
При неудачном READ core не выполняет pop/increment; partial-register state
при abort пока не заявляется идентичным DCJ11.

Нечётный конечный target может быть записан в PC: odd-word fault возникает
при следующем FETCH. JMP/JSR не читают конечный target даже в I/O page.
Indexed displacement и indirect pointers всегда word. SP/PC wrap остаётся
16-битным. Vector frames, stack-limit traps и IRQ пока не реализованы.

## SOB и synthesis gates

Baseline: 361 words, без нового RTL datapath. SOB сохраняет PSW в T4,
извлекает offset из MDR в T5, декрементирует Rs, проверяет Z, восстанавливает
PSW. 8 clocks при завершении / 9 при переходе с ideal RAM.
CP11a core: 673/277/4, 35 MHz PASS, Fmax 36.817.
CP11b FRAM: 933/394/4, 29.56 MHz PASS, Fmax 31.257.

Эксперимент v6 `FETCH_Z` проверяет ALU Z без записи PSW: 2/3 CPI, 355 words.
Но core CP11c не проходит 35 MHz: 674/277/4, Fmax 33.929; путь
EBR→RF→ALU→Z→microsequencer→EBR, 29.499 ns / 21 levels, violation 0.902 ns.
FRAM CP11d: 971/394/4, 29.56 MHz PASS, Fmax 30.443.

Принят v7 `FETCH_A1`: ALU sequencing 3 завершает instruction, если исходный
RF[A] равен 1, иначе идёт на следующую microinstruction. SOB выполняет
Rs−1 за первый execution cycle; при старом Rs=1 сразу retires, иначе
второй ALU cycle вычитает offset из PC. PSW не записывается. Predicate
не зависит от операции ALU и проверяет все 16 bits, не только low byte.

DISP использует IR[14] как контекст: branch имеет signed IR[7:0]×2;
SOB — unsigned IR[5:0]×2. Это два дешёвых маскирующих бита. Только branch/SOB
routines сейчас выбирают DISP. При новом применении поля контекст нужно
проверять. `FETCH_A1` запрещает один speculative launch; следующий PC write
тоже не запускает speculation. Обычные word/byte ALU sequences не меняют policy.

Final CP11e core: **694 LUT/277 FF/4 EBR**, 35 MHz PASS, Fmax **36.358**.
CP11f FRAM/prefetch: **970/394/4**, 29.56 MHz PASS, Fmax **31.672 MHz**.
355/1024×36 microinstructions. Цена относительно CP10: +35 LUT core,
+65 LUT FRAM, без новых FF/EBR. Full peripheral top и board pin timing не входят
в эти цифры. 50 MHz остаётся открытой целью. Для probe остаётся 310 LUT / 3 EBR.

## Проверки и benchmark programs

14814 завершённых DCJ11 fixtures: JMP 910, JSR 8504, RTS 920, SOB 4480.
Все legal mode/register/link combinations, все 64 SOB offsets, NZVC,
SP/PC aliases, odd final targets, stack overlaps и wrap. 30956 exact bus beats.
Из 15270 кандидатов исключены 456: 448 vector/trap, 402 I/O, overlap 394.
В частности, yellow-stack trap может обслужиться и очистить fTrap до возврата
core_step. Поэтому отдельная build copy получает read-only vector-entry hook
в дополнение к четырём address hooks. Исходный DCJ11 emulator не менялся.

58 directed cases: illegal mode 0, pointer/stack ACK errors, odd word pointers/
stack, no final target read, link/SP/PC ordering, 16-bit early predicate и waits.
Последовательности с вложенными JSR/RTS проверяют сохранение link registers и SP.
Небольшая PDP-11 программа суммирует 1..16 в подпрограмме ADD/SOB и возвращает
136 через RTS PC. Benchmarks выполняют warmup и измеряют полные loop periods.

Полная portable/vendor регрессия: 104991 completed DCJ11 cases на каждую
RAM/FRAM × ROM пару и 638 benchmark runs на ROM-модель. Прежние 606
benchmarks и CP10 fixtures/cycles сохранены. [Manifest](verification-cp11.json),
[benchmarks](benchmarks-cp11.json), [portable log](../tb/reports/cp11-tests.log),
[vendor ISA](../tb/reports/cp11-vendor-isa.log).
MMU не входит в CP11 или следующие этапы первой версии.
