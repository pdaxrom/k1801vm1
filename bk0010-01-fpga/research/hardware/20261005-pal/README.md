# Аппаратная проверка положения PAL

5 октября 2026: исправленная прошивка записана во Flash hardware-lcd
через Linux-стенд uJ11 sash@192.168.1.108.
Flash Erase,Program,Verify завершён успешно за 53 секунды.
UART передал B:01…B:07/B:FF без ошибок: прочитан LOADERBK.BIN,
ПЗУ загружено, CRC32 потока и чтения SRAM совпали, установлен ROM lock.
SD-карта не переписывалась. UART закрыт после проверки.

Кадр сдвинут на 8 пикселей влево и 4 строки вниз; все 512×256 точек сохранены.
Пользователь проверил верхнюю строку загрузчика, «ЛАТ» и закрывающую скобку
BASIC справа и подтвердил: **«Всё помещается»**.

[Релиз](../../../releases/hardware-lcd-pal-20261005/README.md),
[программатор](flash-session/02-program.log),
[UART](flash-session/uart.bin), [сессия](flash-session/session.json),
[результат](result.json), [SHA-256](manifest.json).

JED SHA-256:
64f4e157355c0d4a16a874aaf23e5651dfb25d8546bae57dcb243c900d0120e9.
Fuse checksum 6086, transmission checksum EED1.
MAP/PAR/TRACE: ноль ошибок setup/hold.

Ручные проверки PS/2, индикатора, четырёх частот и длинного RESET
сохранены в [первой сессии](../20261004/README.md),
совместная SD-карта Пълдина и БК — в [сессии LOADERBK](../20261004-loaderbk/README.md).
В этой сессии проверены исправленные края кадра и запуск с SD.
