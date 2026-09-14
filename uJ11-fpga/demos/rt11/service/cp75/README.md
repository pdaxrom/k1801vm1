# CP75: MODF/MODD в HALT FRAM

Пакет CP67b без MMU: управление, transfers, unary/compare,
ADD/SUB/MUL/DIV и **MOD F/D**. 32 мнемоники / 2429 корректных кодировок.
Преобразования пока не реализованы. [Полный отчёт](../../../../docs/fp11-mod-cp75.md).
На физическую плату пакет не установлен; перепрошивка FPGA не требуется.

| Файл | Назначение |
|---|---|
| FP11.BIN | Absolute HALT module, BASE 040000, CP67 ABI3 |
| FP11.MAC | DEC MACRO-11 исходник, версия 00.08 |
| FPTST.SAV / FPTST.MAC | 29 FP STEP и самостоятельные проверки под RT-11 |
| release.json | SHA256, immutable length и полная аллокация |

Резерв **040000–046533**, 3420 байт HALT FRAM с BSS/stack.
Checksum `126543` покрывает 3154 байта кода. Файл BIN занимает 4096 байт
(восемь RT-11 блоков, включая header). MEMEND=`046534`;
автоматического размещения BSS и relocation нет.

Загрузчик — [UJMOD.SAV CP67](../cp67/UJMOD.SAV). После копирования файлов
на системный диск выполнить `RUN UJMOD`, `STATUS`. Если FP уже активен,
выполнить `OFFn` для его номера и длинный RESET, затем загрузить новый FP:

```text
.RUN UJMOD
UJMOD> FP11
```

Длинный RESET без UART ESC проверяет checksum, очищает FP-состояние и
включает FP-ready; status=`140407`. ODT/SDBOOT сохраняются. `OFFn` и
cold RESET отключают FP. UART ESC в стартовом окне обходит все модули.

Проверка через ODT после cold init: `RUN FPTST`, короткий RESET,
`R 7 1012`, **29 команд S**, затем `R 7 1154`, `C`.
Программа проверяет сохранённые результаты, отрицательные части MOD,
округлённое D 1/3 и divide-zero; при успехе выводит `RETURNED TO RT11`.

Адреса относятся только к приложенному SAV: после сборки использовать
MAP/listing. STEP начинать после cold init; SETD сохраняет NZVC, поэтому
повторный прогон без инициализации может отличаться по flags. FP immediate
`#1.0` — floating constant MACRO, raw `#40200` не эквивалентен.
ODT пока выводит FP как `.WORD`; FP disassembly и AC/FPS dump в TODO.

RT-11/UJMOD/ODT, самопроверка и cold init/OFF в симуляции:
**80 checks / 689803088 clocks / 6825 UART bytes — PASS**.
