# DEC BASIC-11: FIS и программный FPP uJ11

Исходный комплект находится в `lsi11/disks/rt11v5.3/basic.dsk` относительно
корня репозитория. Его `BASIC.SAV` сообщает **BASIC-11/RT-11 V2.1**.
В комплекте есть библиотеки `BSOT0S/BSOT1S.FIS`,
`BSOT0S/BSOT1S.FPU` и `BSOT0D/BSOT1D.FPU`, а также SUCNFG.
Готовая штатная BASIC.SAV использует программную арифметику и сама по себе
не проверяет ни FIS, ни FPP.

Сборка выполняется оригинальными программами DEC согласно
[Installation Guide, §4](https://bitsavers.org/pdf/dec/pdp11/lang/basic/basic-11/DEC-11-LIBTA-A-D_BASIC-11_RT11_V2_Installation_Guide197803.pdf).
FIS поддерживает только single; FPU — single и double. Выбор FPU в SUCNFG
не меняет профиль нашей эмуляции: на uJ11 используется программный FPP
CP80 с семантикой DCJ11. Дополнительного RTL нет.

## Воспроизведение сборки и эталона

Из корня репозитория, с установленными SIMH `pdp11` и `lsi11/rt11tool`:

```sh
python3 uJ11-fpga/tools/build_basic_cp81.py --out uJ11-fpga/build/basic-new
python3 uJ11-fpga/tools/test_basic_cp81.py \
  --built uJ11-fpga/build/basic-new --out uJ11-fpga/build/basic-reference-new --errors
```

Оба каталога должны быть новыми. Исходные диски не изменяются.
SUCNFG запускается из штатного BASIC; создаются `.COM`, затем LINK создаёт
`.SAV` и `.MAP`. Запрашиваются background, PRINT USING, transcendental,
SUB, RESEQ, длинные ошибки и overlay type 2; CALL отключён.
Три undefined globals `..UAC$`, `..NRC$`, `..MSP$` предусмотрены SUCNFG
при таком выборе CALL. Другие ошибки сборки не разрешаются.

| Файл | Арифметика | Размер SAV |
|---|---|---:|
| [B81FIS.SAV](../../../tb/reports/cp81-basic/build/B81FIS.SAV) | FIS, single | 27136 байт |
| [B81FPU.SAV](../../../tb/reports/cp81-basic/build/B81FPU.SAV) | FPU, single | 26624 байта |
| [B81FPD.SAV](../../../tb/reports/cp81-basic/build/B81FPD.SAV) | FPU, double | 27136 байт |
| [B81FIJ.SAV](../../../tb/reports/cp81-basic/adapter/B81FIJ.SAV) | FIS, single, адаптер стека для FPP | 27136 байт |

SIMH проверяет FIS на 11/03 с EIS+FIS, FPU на 11/73. Это эталон,
а не свидетельство выполнения на FPGA.

## FIS и FPP в одной системе

RT-11 с активным FPP передаёт .SFPA-обработчику дополнительные FEC/FEA.
Старый B81FIS этого не учитывает: обычная арифметика проходит, а ошибка
FIS повреждает обработку стека. Для uJ11 подготовлен отдельный B81FIJ с
26-байтовым адаптером. Он прошёл SIMH, полный RTL и физическую плату,
включая повторные арифметические ошибки. Установлен 2026-09-20;
на uJ11 с активным FPP следует запускать **B81FIJ**. Подробности — в
[журнале CP81](../../../docs/basic-cp81.md).

```sh
python3 uJ11-fpga/tools/build_basic_fis_abi_cp81.py \
  --built uJ11-fpga/build/basic-new --out uJ11-fpga/build/basic-fis-adapter-new
python3 uJ11-fpga/tools/test_basic_cp81.py \
  --built uJ11-fpga/build/basic-fis-adapter-new \
  --out uJ11-fpga/build/basic-fis-adapter-reference-new --errors --fis-adapter
```

Это оригинальная арифметика DEC плюс 26 байт MACRO-11-кода адаптации
стека, без новых ресурсов FPGA. Исходный B81FIS остаётся для сравнения.

При отключённом FPP после холодного старта можно использовать исходный
**B81FIS без изменений**. Оба FIS-варианта проверены в такой конфигурации
на плате, включая ошибки и повторное деление на ноль:
[проверка и переключение модулей CP82](../../../docs/modules-cp82.md).

## Запуск в RT-11

Поместить нужные SAV и `B81TST.BAS`, `B81DBL.BAS` на `DK:`.
Текст BAS при передаче с хоста должен иметь CRLF. Не заменять штатный
BASIC.SAV: имена B81 оставлены отдельными для сравнения.

```text
RUN B81FIJ
OPTIONAL FUNCTIONS (ALL, NONE, OR INDIVIDUAL)? A
READY
RUN B81TST
CP81 BEGIN
CHECKS= 29  ERRORS= 0
CP81 PASS
READY
BYE
```

Повторить с `RUN B81FPU`, затем `RUN B81FPD`.
Для B81FPD дополнительно выполнить `RUN B81DBL` до `BYE`:
ожидается `CP81 DOUBLE PASS`. Этот второй тест предназначен именно для
double и закономерно не проходит в single.

На uJ11 FIS работает в микрокоде. Обоим FPU-вариантам нужен активный
FRAM-модуль FP11 CP80; способ установки описан в
[аппаратном журнале CP80](../../../docs/board-fpp-cp80.md).
В ROM и FPGA ничего из BASIC не добавляется.

Все четыре SAV и оба BAS уже установлены на SD платы. B81FIS сохранён
для сравнения; его обработчик ошибок несовместим с активным FPP.

## Что проверяют программы

`B81TST.BAS` выполняет 29 сравнений: арифметику и диапазон чисел,
ABS/SGN/INT, степень, SQR/SIN/COS/ATN/LOG/LOG10/EXP, массивы,
целочисленный цикл, гармоническую сумму, факториал, метод Ньютона,
READ/RESTORE и GOSUB. Допуск `3e-5 * (1 + abs(expected))` выбран для
общего теста single/double и библиотечных приближений. Это прикладной
smoke test, а не проверка последнего бита всех FPP операций.

`B81DBL.BAS` требует `(16777216+1)-16777216 = 1` и сохранения разности
`1.000000000001 - 1` с допуском `1e-15`. Эталон SIMH печатает
`DOUBLE DELTA= 1  9.99978E-13`; формат PRINT по умолчанию сам ограничивает
число видимых цифр.

Опция `--errors` дополнительно выполняет в каждом BASIC `PRINT 1/0`,
`PRINT SQR(-1)`, `PRINT 1E30*1E30` и `PRINT 2+2`. Ожидаются соответственно
`?DIVISION BY ZERO`, `?NEGATIVE SQUARE ROOT`, `?FLOATING OVERFLOW`, затем
число 4. После каждой команды BASIC должен вернуться в READY. Это
отдельная проверка обработки ошибок; RTL-сценарий ниже выполняет только
числовые программы B81TST/B81DBL.

Для полного CP67b RTL после сборки CP80-модулей:

```sh
python3 uJ11-fpga/tools/run_basic_rt11_cp81.py \
  --built uJ11-fpga/build/basic-new \
  --programs uJ11-fpga/build/basic-reference-new \
  --out uJ11-fpga/build/basic-rtl-new
```

Стенд использует настоящие CPU/FRAM/SD/UART и устанавливает модули через
UJMOD. Время окна выбора recovery сокращено до одной итерации; это
единственное ускорение boot firmware. CPU-состояние не подменяется.
Счётчики отделяют FIS от входов в HALT FPP и отслеживают внешние обращения
к USER `177776` на протяжении сеанса BASIC, включая его запуск и BYE.
