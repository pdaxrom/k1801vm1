# После FIS: общий HC1200 top и RT-11

Этот документ фиксирует следующий integration gate, **не готовую сборку
для платы**. Текущий CP27b —1095 LUT/416 FF/4 EBR с FRAM, prefetch, IRQ adapter
и probe. Полного peripheral fit и загрузки RT-11 у uJ11 пока нет.

## Что уже проверено в lsi11-fpga

Изучены `boards/hc1200-microcomp/am4_microcomp.v`, `am4_cpu11_bus.v`,
`docs/PORTING-NOTES.md`, frozen bus/FRAM/UART/SD models в `reference/lsi11/`
и текущие uJ11 peripheral RESET/IRQ tests. Память остаётся MR45V100A FRAM.

Последняя описанная в PORTING-NOTES программированная сборка AM4 от
2026-09-05 содержит **1271/1280 LUT, 639/640 slices и 7/7 EBR**, ноль
unrouted connections, setup slack 1.543 ns и hold slack 0.304 ns при CPU
29.56 MHz. В документе зафиксированы физическая загрузка RT-11, DIR,
RK/UART high-byte tests и запись MACRO OBJ/LST. Это результат AM4, не uJ11.
AM4 действительно помещает всю систему, практически исчерпывая FPGA.

AM4 хранит bootstrap/RK firmware в spare bits семи microcode EBR и небольшой
LUT extension. У uJ11 36-bit store занимает четыре EBR; формат AM4 packing
переносить не требуется. Сначала надо измерить отдельный firmware ROM в
составе полного top и выбрать его реализацию по MAP/PAR.

## Конкретные условия подключения

* Общий top включает core, один FRAM controller, KL11 115200 8N1, KW11-L
  50 Hz, panel 166000/1, SD byte service 177500/2, bootstrap и RK service.
  Прежний probe baseline сохраняется для сравнения; board results считаются
  отдельно, с OSCH/reset и реальным pinout.
* Bus adapter переводит right-justified uJ11 byte data в AM4 byte lanes и
  обратно. High-byte CSR write должен ACK без изменения low-byte состояния.
  RXBUF read-clear и SD read-clock-byte происходят ровно один раз на ACK.
* Нельзя просто подключить нынешний `io_request` ко всему AM4 bus: bootstrap
  отображается ниже I/O page, а RK firmware различает instruction fetch и
  data accesses. Это должно быть явно выражено на границе core/bus.
* Prefetch разрешён только для обычной FRAM. Bootstrap release, вход/выход
  RK service и любое изменение отображения отменяют buffered stream.
  Demand access не должен получить слово от прежнего отображения.
* RK register backing использует частную периферийную часть FRAM. Она не
  расширяет CPU address space, остающийся 16-bit. AM4 service DMA bypass
  для I/O-page addresses нельзя случайно применить к обычной CPU инструкции.
* Reset uJ11 сейчас обнуляет PC/SP и начинает fetch с нуля. AM4 startup/ODT
  contract другой. Нужен явный board bootstrap entry без записи регистров
  тестбенчем; копирование только overlay 024/026 не запустит этот core.
* uJ11 HALT имеет документированный DCJ11 restart profile. Native AM4 ODT
  нельзя считать уже перенесённым. Проверка RT-11 должна учитывать реальный
  старт и возврат в монитор, а не подмену PC после reset.

## Последовательность измерений

1. Собрать общий bus/ROM/периферию с legacy FRAM handshake, проверить
   reset/bootstrap и единичные side effects; сразу получить полный HC1200
   synthesis baseline. При превышении ресурсов выделить дорогие mux/decode/
   дублированное состояние по report до следующих функций.
2. Подключить sequential FRAM/prefetch через тот же demand contract и
   измерить разницу площади/частоты/тактов в полном top. Сохранять prefetch
   по измеренному выигрышу, не полагаясь на разницу неполных probes.
3. Прогнать real SD bootstrap и RK READ/WRITE с существующим RT-11 image и
   SD model из lsi11-fpga: banner, prompt, DIR, запись файла, IRQ/WAIT и Ctrl-C.
   Сверять секторные запросы и содержимое записанного образа.
4. Проверить vendor ROM, общий MAP/PAR/TRACE и внешние pin delays. Только
   этот результат показывает остаток ресурсов системы. Физическое
   программирование и проверка на плате учитываются отдельным результатом.

Цель по-прежнему — сохранить место для routing и обвязки; AM4 1271 LUT не
становятся целевым бюджетом uJ11. FP11 рассматривается после этого baseline.
MMU не входит в этот этап и не получает reserved datapath или EBR.
