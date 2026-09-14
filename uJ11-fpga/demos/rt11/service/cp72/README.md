# CP72: ускоренные переносы AC в FP11-A firmware

Пакет для CP67b без MMU: те же управляющие команды, LDF/STF и
CLR/TST/ABS/NEG/CMP F/D, что в CP71. Mode-0 LDF/LDD быстрее на 17–19%,
STF/STD на 6–7% в измеренной SPI FRAM программе.
[Изменения, проверки и ограничения](../../../../docs/fp11-paths-cp72.md).
ADD/SUB/MUL/DIV и преобразований ещё нет. На физическую плату пакет
не установлен; перепрошивка FPGA не требуется.

| Файл | Назначение |
|---|---|
| FP11.BIN | Absolute HALT module, BASE 040000, CP67 ABI3 |
| FP11.MAC | DEC MACRO-11 исходник, версия 00.05 |
| FPTST.SAV / FPTST.MAC | Прежний тест CP71: 11 FP STEP, самопроверка и возврат |
| release.json | Hashes, immutable length и полная аллокация |

FP11 занимает **040000–043127, 1624 байта** HALT FRAM с BSS/stack.
Checksum покрывает 1394 байта кода; файл — четыре RT-11 блока.
Весь диапазон до MEMEND=`043130` нужно резервировать: таблица модулей
хранит только immutable length, автоматического BSS allocator/relocation нет.

Использовать [UJMOD.SAV CP67](../cp67/UJMOD.SAV). После копирования файлов
на системный диск выполнить `RUN UJMOD`, `STATUS`. Если FP уже активен,
сначала `OFFn` и длинный RESET, затем установить новый файл:

```text
.RUN UJMOD
UJMOD> FP11
```

Индекс n берётся из STATUS. После следующего длинного RESET без UART ESC
модуль проходит checksum, очищает FPS/FEC/FEA/AC и включает FP-ready.
Ожидаемый статус — `140407`. ODT/SDBOOT сохраняются. Для отключения —
`OFFn` и cold RESET. UART ESC в начальном окне выбирает встроенный bootstrap
с обходом всех FRAM-модулей.

FPTST побайтно совпадает с CP71: использовать
[ту же последовательность STEP и адреса](../cp71/README.md).
`RUN FPTST`, короткий RESET, `R 7 1012`, одиннадцать `S`, затем
`R 7 1056` и `C`. Программа проверяет результаты и возвращается в RT-11.
SETD сохраняет прежние NZVC; для повторного STEP учитывать пояснение CP71.
ODT пока показывает FP opcode как `.WORD`; отдельного dump AC/FPS/FEC/FEA нет.

В модели SPI SD/FRAM повторена установка UJMOD, 11 STEP, самопроверка FPTST,
DIR, cold init и OFF: **43 checks / 531124415 clocks / 3476 UART bytes — PASS**. Это симуляция, а не запись на плату.
