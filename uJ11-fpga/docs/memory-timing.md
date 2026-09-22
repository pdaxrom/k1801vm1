# SPI FRAM и внешний timing

## Основа по документации

[MR45V100A, FEDR45V100A-01](https://www.mouser.com/datasheet/2/348/FEDR45V100A-01-1280312.pdf),
страницы 7 и 15: READ `03` допускает SCK до **34 MHz**, остальные команды —
до 40 MHz. Для READ нужны high/low SCK не короче 13 ns; CS setup/hold и
время между транзакциями — 10 ns; MOSI setup/hold — 5 ns. Максимальная
задержка MISO после спада SCK — 12 ns при VCC >=2,7 V, 13 ns ниже.
Минимальная задержка изменения MISO — **0 ns**. Нельзя предполагать, что
старый бит держится после прихода следующего спада SCK на FRAM.

[MachXO2 High-Speed Interfaces, FPGA-TN-02153](https://www.latticesemi.com/view_document?document_id=39084),
§5.4.1 и §13.3: выходной ODDRXE позволяет передавать clock на pin;
SCLK должен использовать primary clock routing. Проверка штатной Diamond
модели подтвердила: D1 фиксируется на posedge SCLK и выходит на следующий
negedge. Поэтому разрешение последнего импульса нужно снимать заранее.

Уточнение после первой локальной проверки: ±5% из общего описания
[sysCLOCK](https://www.latticesemi.com/view_document?document_id=39080)
недостаточно для полного timing budget. В
[MachXO2 Family Data Sheet, FPGA-DS-02056-4.7](https://www.latticesemi.com/view_document?document_id=38834),
§3.24, таблица 3.30, приведены 125,685/133/140,315 MHz для commercial
OSCH, скважность **43–57%**, period jitter до **0,02 UIPP**; для более
низких частот относительный jitter меньше. Для проверки принят envelope
**+5,5%**, 43/57 и дополнительно полный 2% period jitter в худшую сторону.
При номинале 29,56 MHz это 31,1858 MHz, период 32,065876 ns. Минимальный
проверяемый период — 31,424559 ns; TRACE округлён консервативно до
**31,824 MHz**. Это принятый проверочный envelope, не измерение генератора.

## Драйвер

Драйвер — [uj11_board_fram.v](../boards/hc1200/uj11_board_fram.v).

- SCK выводится через настоящий **ODDRXE**, D0=0, D1=разрешение импульса.
  В synthesis не входит portable модель `tests/models/ODDRXE.v`.
- На posedge CPU SCK переходит в 0, выставляется следующий MOSI;
  на negedge CPU SCK переходит в 1.
- MISO фиксируется на следующем posedge CPU, **до того как исходящий спад
  SCK дойдёт до FRAM и изменит её выход**. Для setup доступна почти целая
  длительность SCK с вычетом задержек. Три физических capture paths
  включены в сохранённый [TRACE](../releases/hc1200/design.twr).
- На `bit_count=7` уже передаётся последний импульс; D1=0 запрещает следующий.
  Между байтами сохраняется один launch cycle: **9 вместо 17 CPU clocks**.
- RX остаётся отдельным, но хранит семь предыдущих битов; восьмой напрямую
  объединяется с MISO при записи результата. Нет делителя/фазового FF,
  PLL, аппаратного prefetch или новых CPU операций.
- `keep_read`, cursor, CS, byte/word, оба банка 128 КиБ, odd error,
  `ready/error/busy`, удержание запроса и прерывание reset сохраняют контракт.
  Поддерживается только `CLK_DIV=1`; иной параметр останавливает simulation.
- Reset маскирует D1 синхронно, как управление CS: текущий высокий
  полупериод не обрезается асинхронным reset примитива.


[Результат текущего MAP/PAR/TRACE](synthesis.md). PCB budgets и SCK pulse width
требуют физического измерения; они не объявляются измеренными на плате.
