# Native VM1 SD bootstrap

Адреса регистров восьмеричные; offsets заголовков десятичные.
Сам ВМ1 выполняет resident BIOS, FAT16 и второй этап. Первичный BIOS
занимает 7048 байт в 8 КиБ EBR; второй этап LOADERBK.BIN — 664 байта,
RAM 020000…027777. Меню и BK.SET описаны в [bios.md](bios.md).

## Этапы

| Hex | Действие |
|---|---|
| 01 | SD initialization ≤400 кГц, CSD capacity |
| 02 | MBR/BPB и FAT16 |
| 03 | LOADERBK.BIN в RAM 020000 |
| 10 | BIOS countdown |
| 11 | BIOS setup |
| 04 | BK00…04.ROM, заголовок и контракт модели |
| 05 | Сегменты в физическую SRAM, CRC32 каждого |
| 06 | Независимое чтение всех сегментов SRAM, общая CRC32 |
| 07 | Подготовка RAM-трамплина |
| FF | ROM lock, DCLO/ACLO, запуск выбранного ПЗУ |

До lock resident ROM виден в 100000…117777, scratch — ниже 010000,
SD buffer — 02000, ROM header — 03000, save buffer — 04000,
stack начинается с 010000 и растёт вниз. Служебные вызовы:
100004 open, 100010 get byte, 100014 fail, 100020 handoff,
100024 stage, 100030 try-open, 100034 проверить контракт сегментов.
Get byte сохраняет R1–R5, show stage — R0–R5.

RAM-трамплин в 0400 устанавливает commit, не исполняя исчезающий resident
ROM. Top-level после handoff сбрасывает CPU по последовательности DCLO/ACLO.
Entry: 100000 для режимов 1/2, 140000 для 3/4, 164400 для А16М.

## ROM bundle v2

Little-endian, заголовок 512 байт, за ним последовательные ROM segments.
Имена BK00.ROM…BK04.ROM выбираются по ID настроек.

| Offset | Размер | Значение |
|---|---|---|
| 0 | 8 | `BKROM2\0\0` |
| 8 | 2 | версия 2 |
| 10 | 2 | header bytes=512 |
| 12 | 2 | ID модели 0…4 |
| 14 | 2 | число сегментов 1…6 |
| 16 | 4 | CPU entry |
| 20 | 4 | общая длина payload |
| 24 | 4 | общая CRC32 payload |
| 28 | 4 | CRC32 header, это поле обнулено при расчёте |
| 32 | 16 × count | дескрипторы сегментов |
| после дескрипторов | до 512 | нули |

Дескриптор: физический байтовый адрес SRAM, длина, offset в payload,
CRC32 сегмента — четыре uint32. ID/entry/count/адреса/размеры/offset
должны точно совпадать с [контрактом](../config/machines.json).
Пустые CPU-окна и CSR не становятся ПЗУ из-за padding файла.

CRC32: reflected `EDB88320`, init `FFFFFFFF`, final XOR `FFFFFFFF`,
совместимо с zlib.crc32. Проверяются header, поток каждого сегмента и
полное независимое чтение SRAM. В аппаратуре CRC обрабатывает байт
за восемь тактов; подтверждение шины выдаётся после завершения.

Старый BK0010.ROM v1 допускается только как fallback режима 2:
magic `BK0010\0\0`, header 512, payload 32768, base/entry 8000h,
payload CRC32 по offset 24, header CRC32 по 28. Payload содержит
MONIT10 8192 и BASIC10 24576. Адреса последних CSR скрыты от CPU,
но их физические байты ПЗУ также проверяются.

## Регистры bootstrap

Доступны только до lock. Word и low-byte операции, кроме disk config,
для которого требуются полные word-записи.

| Адрес | Назначение |
|---|---|
| 177600 | SPI low data, запись запускает byte |
| 177602 | SPI control/READY: 21h selected / 23h deselected |
| 177604 | SPI half-period divider: 29 → 400 кГц, 1 → 6 МГц при system 24 МГц |
| 177606 | SPI high data, резерв 16-bit ABI |
| 177610 | BOOT_STATUS; запись 0 очищает счётчики/CRC незавершённой загрузки |
| 177612 | Stage; запись bit15 устанавливает boot_error |
| 177614 | SRAM pointer low word |
| 177616 | Запись low byte в SRAM, pointer++, written_count++, CRC |
| 177620 | Чтение byte SRAM, pointer++, verified_count++, CRC |
| 177622 | Подать byte в CRC для header |
| 177624 | Reset CRC и verified_count |
| 177626 / 177630 | Final CRC low/high |
| 177632 / 177634 | Expected CRC low/high |
| 177636 | Commit A55Ah после exact written/read counts и CRC match |
| 177640 | SRAM pointer high nibble; включает физическую адресацию |
| 177642 | Ожидаемое число загруженных байт; 0 означает 65536 |
| 177644 | ID модели 0…4 |
| 177646 | Индекс частоты 0…6, bit3 COLOR |
| 177650 | BIOS tick50 pending; чтение очищает, новый tick имеет приоритет |
| 177652 | Disk config: bit0 SDHC, RO bits8…11, bit15 seal |
| 177654 | Указатель слова таблицы дисковых дескрипторов |
| 177656 | Data word, auto-increment при записи |

Disk table words 0…15: A/B/C/D, по четыре слова start low/high и length
low/high. Words16…19 — границы FAT;20…21 — число секторов SD. Seal запускает
проверку bounds/overflow/overlap; чтение bit15 возвращает готовность
только после завершения. Valid drive mask и базы фиксируются до полного
возврата BIOS. Runtime LBA дополнительно ограничен offset<1600 и RO.

Физические stream-порты разрешают только ROM 20000h…2FFFFh.
Commit проверяет отсутствие boot_error, exact общий written_count,
verified_count и общую CRC32. После него bootstrap-порты скрыты,
записи CPU в ROM не подтверждаются, SD принадлежит runtime controller.
Тёплый reset сохраняет ROM/model/дескрипторы; полный десятисекундный
reset или power-on возвращает BIOS. Peripheral RESET очищает интерфейсы,
завершая dirty-дорожку без потери данных. Инструкция RESET сохраняет
банки памяти, видеорегистр 177662 и прокрутку 177664; ожидающие прерывания
снимаются, но разрешение кадрового IRQ100 сохраняется.

## Ошибки загрузки

| Hex | Причина |
|---|---|
| 11 | MBR/раздел/границы/переполнение LBA |
| 12 | BPB/геометрия FAT16 |
| 13 | LOADERBK.BIN больше 4096 байт |
| 21 | Timeout SPI READY |
| 22 | Нет R1 response за 512 байт |
| 23 | SD initialization timeout/response |
| 24 | CMD17/token/адрес SDSC |
| 31 | Файл отсутствует, size/cluster/FAT chain |
| 41 | ROM header/модель/контракт/CRC |
| 42 | CRC потока ROM segment |
| 43 | Общая CRC чтения SRAM |
| 44 | ROM bundle выбранной модели отсутствует |

Отказ до handoff не устанавливает ROM lock. UART передаёт E:xx,
PAL показывает код; RESET позволяет повторить загрузку. Ошибки сохранения
BIOS 50…56 возвращают меню и описаны отдельно в bios.md.
