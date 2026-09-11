# CP56a — установка на HC1200, 2026-09-11

По явному запросу пользователя **CP56a записан во FLASH платы** на
`sash@192.168.1.108`. **FLASH Erase,Program,Verify PASS**, после записи
получены **RT-11FB (S) V05.03**, startup-команды и приглашение монитора.

## Точная прошивка

- Профиль `--spi-cp56`: CP54b с ускоренным FRAM SPI через ODDRXE,
  CPU/FIS и полный набор периферии. MMU и FP11 отключены.
- **1184 LUT / 339 FF / 6 EBR / 595 slices**, 954 слова микрокода.
- Номинальные CPU и FRAM SCK **29,56 MHz**; ранее SCK был 14,78 MHz.
  **31,996 MHz** — архивный внутренний TRACE Fmax, не измеренная частота платы.
- [Установленный JED](../synth/releases/cp56a/design.jed), checksum **80A7**.
- SHA256: `fa2d885d05af35f36e87b3e9ac4498441ed1998a640e76d57a13ac142850a863`.
- Input revision: `4cd485b0a38c6b83fea748333b0284a4725f5067e91c2188b3f28349dd27ae69`.

JED экспортирован Diamond из существующего routed CP56a. До и после
экспорта проверены hashes исходников, synthesis reports и NCD/PRF/EDIF;
повторного synthesis или PAR не было. XCF создан из точного JED, checksum
и timestamp; при повторной записи после checkout XCF нужно создать заново.

## Проверка платы

JTAG chain и FLASH Verify ID прошли с первой попытки для LCMXO2-1200HC
(ожидаемый ID `0x012BA043`). Programmer выполнил erase/program/verify за
20 секунд: `Operation Done. No errors.`, `Operation: successful.`.
[Журнал](../tb/reports/cp56a-hardware/program/hardware-1/programmer.log).

На `/dev/ttyUSB1` захвачены 74 байта:
RT-11FB (S) V05.03, `SET TT SCOPE,NOCRLF`, `SET SL ON` и prompt.
[UART capture](../tb/reports/cp56a-hardware/program/hardware-1/uart.bin).
Команды с хоста не посылались; SD-карта образом не перезаписывалась.
Picocom PID 128617 был временно приостановлен; termios восстановлены,
picocom возобновлён, состояние процесса проверено (`S`).

Плата оставлена у приглашения RT-11 для пользовательских тестов.
Для клавиатуры/HG вернуть **JTAG_EN в GPIO**; scanner и HG используют общие
pins и проверяются по очереди. Программы, RGB/HDSP, keyboard и HG на CP56a
пока не объявляются проверенными. Предыдущая прошивка
[CP54b](board-bringup-cp54b.md) сохранена для возврата.

Успешная загрузка подтверждает работу в текущих условиях. Измерение формы
SCK/PCB задержек и полный pulse-width timing остаются открытыми задачами
[CP56](spi-cp56.md); консервативный расчётный запас короткого SCK — 0,147 ns.
Default сборки пока CP52a; установленный вариант выбирается явно `--spi-cp56`.

[Машиночитаемый отчёт и hashes](board-bringup-cp56a.json).
