# Тактирование и принятие Diamond-сборки

Вход 12 МГц → main PLL 24/48 МГц; второй PLL → 64 МГц PAL.
Registered divider от 48 МГц формирует CPU p/n 3/6/12/24 МГц с duty 50%.
Режим меняется на rising p после полного старого периода и только при
свободной внешней CPU-шине. Одна DCCA разводит CPU clock; отрицательная фаза
использует инверсию в clock polarity mux регистров/EBR. Поэтому TRACE видит
одну сеть с положительным и отрицательным фронтами, без отдельного skew дерева n.
Импульсы, отображение применённого режима, wrap и длинное нажатие
проверяет `tb_panel.v`; full SoC переключает частоту во время BASIC.

CPU/system мост использует request/response toggle и удерживаемый payload.
При отмене CPU strobe мост завершает/дренирует старую транзакцию и не выдаёт
поздний ACK следующему запросу. Reset утверждается асинхронно, освобождается
через три стадии соответствующего домена. PAL использует отдельные reset pipes,
mailbox конфигурации и descriptor строки, два EBR-буфера по 32 слова.
Режимы кнопки передаются Gray code; для 24→3 тоже меняется один бит.

SRAM работает только от 24 МГц. Setup → два access → hold → release,
с полным интервалом смены владельца DQ. Арбитр даёт каждому клиенту по одному
слову; при конкуренции чередует CPU и PAL. Тест `tb_video.v` создаёт постоянную
CPU-нагрузку и проверяет все видимые пиксели в четырёх полях, scroll/RP,
моно/цвет и реакцию на остановку SRAM. Дополнительно сверяются края окна
718…3917 тактов / 32…287 и 345…600 строк и пятистадийная задержка монохромного DAC. Модель `sram_model.v` задерживает
чтение на 10 нс и проверяет ширину WE/стабильность адреса, DQ и byte lanes.
В Verilator задерживается read value; четырёхсостоянийный `tb_memory.v`
дополнительно проверяет byte lanes, смену владельцев и reset во время WE.
Физические времена остаётся проверить на плате.

`pins.lpf` задаёт выводы hardware-lcd, частоты и внешние SRAM/SD budgets.
`cdc.lpf` блокирует только первые стадии toggle synchronizers и задаёт
MAXDELAY удерживаемых mailbox payloads. Асинхронное утверждение и recovery
на входах reset pipes исключены; пути clocked release pipe→consumer проверяются.
Оба LPF объединяются в один активный `clock.lpf`: Diamond активирует только
один LPF одновременно. FDC задаёт точные generated clocks для Synplify.
Clock nets имеют `syn_keep`, synchronizer registers — `syn_preserve`.

На Linux-стенде `sash@192.168.1.108` выполнены MAP/PAR/TRACE Diamond 3.14
для Industrial, speed grade 4. Отчёты и прошивка сохранены в
[releases/hardware-lcd-pal-20261005](../releases/hardware-lcd-pal-20261005/README.md).
Ноль ошибок setup/hold; все явные частоты, SRAM/SD I/O, mailbox MAXDELAY
и BLOCK PATH имеют ненулевое число scored items. Отдельные diagnostic
MULTICYCLE 1X добавляют ноль тактов и выводят p→n и n→p с бюджетом половины
периода. Проверка не использует общий false-path между clock domains.

Synplify сообщает четыре MT688 о black-box моделях PLL/DCCA. Его Clock Summary
подтверждает заданные 24/48/64/24 МГц; TRACE проверяет физические примитивы
и межтактовые пути. Gate допускает только эти четыре точных предупреждения.
Неконстрейненные CLOCK_TO_OUT относятся к аудио, LED, индикатору, UART,
PS/2 inhibit и аналоговому PAL DAC — внешнего clocked capture у них нет.
Неизвестные unconstrained paths или пропавшее LPF-ограничение останавливают сборку.

При принятии реализации нужны следующие реальные результаты:

1. Synthesis/MAP/PAR без ошибок, все сигналы разведены, LUT/FF/EBR в пределах
   LCMXO2-7000HC, boot ROM и строковые буферы действительно реализованы в EBR.
2. TRACE оценивает 24/48/64 МГц и обе CPU-фазы. Межфазные p→n/n→p пути
   имеют бюджет половины периода (20.833 нс при 24 МГц); проверены setup и hold.
   Частотное ограничение общей сети учитывает оба фронта; setup и hold
   не заменяются ограничением максимальной задержки всего CPU.
3. Все SRAM INPUT_SETUP/CLOCK_TO_OUT и mailbox MAXDELAY preferences имеют
   ненулевое число scored items и ноль timing errors. Unmatched preferences
   исправлены, synchronizer stages сохранены. Незнакомые unconstrained paths
   разобраны; blanket false-path между доменами не используется.
4. Проверены SD SCK/MOSI launch, MISO capture и выбранные SPI divider;
   модель карты подтверждает протокол, но не измеряет физические setup/hold SD.
5. После экспорта выполнены функциональные проверки на плате: загрузка,
   PS/2/BASIC/STOP, смена частот, long reset, сохранение ПЗУ и чтение SRAM.
   Проверены частоты кварца/PLL, CPU pulse shape и skew p/n осциллографом.
6. PAL: 75 Ом, уровни sync/black/white, interlace, цветовая синхронизация,
   отсутствие пропусков строк и читаемость 512 точек.

Методика timing preferences следует [Lattice Timing Closure](https://www.latticesemi.com/view_document?document_id=45588),
разводка DCCA — [MachXO2 sysCLOCK](https://www.latticesemi.com/view_document?document_id=39080).
SRAM предполагает tAA 10 нс и PCB round trip 2 нс. SD работает в fast mode
6 МГц, half period 83.33 нс: MISO arrival budget 26 нс, внешний стабильный
hold window 10 нс (в LPF задаётся как HOLD -10 из-за определения знака TRACE).
Эти budgets и device из схемы требуют подтверждения для фактической платы.
JED принят по статическим проверкам; аппаратные измерения остаются отдельным этапом.
