# pico-mk90

Порт эмулятора MK-90 для Raspberry Pi Pico / Pico 2.

## Подключение

Используется та же распиновка, что в `pico-lsi11`.

SD card (`SPI0`):

- `MISO` = `GP16`
- `CS` = `GP17`
- `SCK` = `GP18`
- `MOSI` = `GP19`

ST7565 (`SPI1`):

- `BL` = `GP9`
- `DC` = `GP10`
- `RST` = `GP11`
- `CS` = `GP13`
- `SCK` = `GP14`
- `MOSI` = `GP15`

Клавиатура берется из USB stdio `stdin`. Обычные символы, Enter,
Backspace, Tab и ANSI-стрелки преобразуются в scan-коды MK-90.

## SD card

На FAT-карте ожидаются файлы:

```text
0:/roms/rom.bin
0:/roms/romt.bin
0:/media/smp0.bin
0:/media/smp1.bin
```

`rom.bin` обязателен. `romt.bin` и оба SMP-образа опциональны. Для удобства
также проверяются fallback-пути в корне карты: `rom.bin`, `romt.bin`,
`smp0.bin`, `smp1.bin`.

## Сборка

Pico / RP2040:

```sh
cmake -S pico-mk90 -B pico-mk90/build -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build pico-mk90/build
```

Pico 2 / RP2350:

```sh
cmake -S pico-mk90 -B pico-mk90/build-pico2 -DPICO_BOARD=pico2 -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build pico-mk90/build-pico2
```

Результат:

```text
pico-mk90/build/pico_mk90.uf2
pico-mk90/build-pico2/pico_mk90.uf2
```

Настройки CMake:

- `PICO_MK90_STEPS_PER_FRAME` default `2000`
- `PICO_MK90_FRAME_US` default `16667`
- `PICO_MK90_SMP_SLOT_SIZE` default `32768`
- `PICO_MK90_LCD_X_OFFSET` default `4`
