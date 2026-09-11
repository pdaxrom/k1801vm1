# CP54b — установка на HC1200, 2026-09-11

По запросу пользователя выбранный **CP54b записан во FLASH платы** на
`sash@192.168.1.108`. Lattice Programmer выполнил **FLASH Erase,Program,Verify**
без ошибок. После записи на UART появились **RT-11FB (S) V05.03**,
команды startup `SET TT SCOPE,NOCRLF`, `SET SL ON` и приглашение монитора.
Пользовательская проверка программ и периферии ожидается.

## Точная прошивка

- Профиль: `--ack-cp54 dma-ack`, CPU/FIS и полный набор периферии,
  sequential FRAM READ, отдельный RX register. MMU и FP11 отсутствуют.
- **1185 LUT / 341 FF / 6 EBR / 595 slices**, 954 слова микрокода.
- Штатная частота **29,56 MHz**. **32,273 MHz** — внутренний Fmax ранее
  выполненного MAP/PAR/TRACE CP54b; slack 2,843 ns.
- [Установленный JED](../synth/releases/cp54b/design.jed), checksum **B240**.
- SHA256: `cf2be5959dc63f3a53284fa04e9de499574f4787a5c9183c3df9f7305b646069`.
- Input revision: `7b222ad8782a487cf0a4a10205c6747e37651632e5b20223b1546ed24c3ebd32`.

JED создан через Diamond Export из сохранённого routed CP54b на сервере.
Перед экспортом и после него проверены все source и MAP/PAR/TRACE hashes;
они совпали с архивом. Повторная synthesis/PAR не выполнялась. XCF создан
заново из точного JED, его checksum и timestamp. Для повторной записи XCF
нужно сгенерировать снова, поскольку timestamp файла после checkout меняется.

## Запись и консоль

Первый read-only ID probe прочитал `0x00000000`. Запись FLASH не запускалась.
После сообщения пользователя о переводе JTAG_EN в JTAG повторная проверка
цепочки и **FLASH Verify ID** для LCMXO2-1200HC прошли.

Затем выполнены erase/program/verify: Programmer сообщил
`Operation Done. No errors.` и `Operation: successful.`. Операция заняла
около 20 секунд. Сохранённый [журнал записи](../tb/reports/cp54b-hardware/program/hardware-1/programmer.log)
и [UART capture](../tb/reports/cp54b-hardware/program/hardware-1/uart.bin)
относятся к этой же операции. Захвачены 74 байта UART; команды с хоста
в RT-11 не посылались.

На время операции и пассивного захвата picocom PID 64304 был приостановлен.
После завершения восстановлены termios и работа picocom; состояние процесса
проверено (`S`, не stopped). HG daemon не был запущен. SD-карта образом
не перезаписывалась. Плата оставлена у приглашения RT-11.

Для клавиатуры/HG следует вернуть **JTAG_EN в GPIO**. Клавиатурный scanner
и HG используют общие pins, поэтому перед HG нужно завершить keyboard demo.
DIR, RGB/HDSP, клавиатура и HG на CP54b ещё не объявляются проверенными:
их протестирует пользователь. Ранее они были проверены на CP29a.

[Машиночитаемый отчёт и hashes](board-bringup-cp54b.json).
Default сборки в исходниках пока CP52a; эта установленная прошивка собрана
с явным профилем CP54b. Эксперимент CP55a на плату не записывался.
Успешная загрузка не является измерением максимальной частоты или полным
закрытием внешнего FRAM timing.
