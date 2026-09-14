# CP73: сложение и вычитание FP11-A в HALT FRAM

Пакет для CP67b без MMU: управляющие команды, LDF/STF,
CLR/TST/ABS/NEG/CMP и **ADD/SUB F/D**. 26 мнемоник / 1685 корректных
кодировок. MUL/DIV, MOD и преобразования ещё не реализованы.
[Семантика, тесты и измерения](../../../../docs/fp11-arithmetic-cp73.md).
На физическую плату пакет не установлен; перепрошивка FPGA не требуется.

| Файл | Назначение |
|---|---|
| FP11.BIN | Absolute HALT module, BASE 040000, CP67 ABI3 |
| FP11.MAC | DEC MACRO-11 исходник, версия 00.06 |
| FPTST.SAV / FPTST.MAC | 18 FP STEP, самостоятельные проверки, возврат в RT-11 |
| release.json | SHA256, immutable length и полная аллокация |

FP11 резервирует **040000–044715, 2510 байт** HALT FRAM с BSS/stack.
Checksum `032371` покрывает 2252 байта кода; FP11.BIN — шесть RT-11 блоков (3072 байта, включая header; уточнено в CP74).
До MEMEND=`044716` нельзя размещать другие модули: таблица хранит только
immutable length, автоматического BSS allocator/relocation нет.

Загрузчик — [UJMOD.SAV CP67](../cp67/UJMOD.SAV). После переноса файлов на
системный диск выполнить `RUN UJMOD`, `STATUS`. Если уже есть активный FP,
сначала `OFFn` для его номера и длинный RESET, затем установить новый FP11:

```text
.RUN UJMOD
UJMOD> FP11
```

Длинный RESET без UART ESC проверяет checksum, очищает FPS/FEC/FEA/AC и
включает FP-ready. Ожидаемый status — `140407`; ODT/SDBOOT сохраняются.
`OFFn` и cold RESET отключают модуль. UART ESC в начальном окне выбирает
встроенный bootstrap с обходом всех модулей FRAM.

Для ODT-проверки: `RUN FPTST`, короткий RESET, `R 7 1012`, **18 команд S**,
затем `R 7 1104`, `C`. Программа проверяет сохранённые результаты,
ABS negative-zero с FIUV и возвращается в RT-11 с `RETURNED TO RT11`.
Адреса относятся только к приложенному FPTST.SAV; после пересборки смотреть
MAP/listing. STEP начинается после cold init; для повторного прогона NZVC
могут отличаться, поскольку SETD сохраняет flags. AC0 перед ADDF очищается
самой программой. FP immediate `#1.0` проверен в native listing как `040200`:
MACRO-11 преобразует такие операнды, поэтому raw `#40200` не эквивалентен.

ODT пока выводит FP opcodes как `.WORD`; отдельные FP disassembly и
AC/FPS/FEC/FEA dump остаются в TODO. Все автоматические проверки этого
пакета — симуляция, не установка на плату.

RT-11/UJMOD/18 STEP, самопроверка и cold init/OFF: **56 checks /
592741136 clocks / 4779 UART bytes — PASS** в модели SPI SD/FRAM.
