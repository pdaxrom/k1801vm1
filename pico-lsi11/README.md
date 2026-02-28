# pico-lsi11

Pico port of the LSI-11 / PDP-11 emulator for Raspberry Pi Pico (RP2040), based on sources from `../lsi11` and `../core`.

## Architecture

The emulator takes advantage of the RP2040 dual-core design:

| Core | Role |
|------|------|
| **Core 0** | Initialization, SD card I/O, peripheral polling (`lsi11_poll_devices`) |
| **Core 1** | CPU emulation loop (`core_step`) |

Shared device register and IRQ state is protected by a hardware spinlock (`io_lock.h`). The spinlock guards:
- `devio_read8` / `devio_write8` — I/O register access from the CPU (core 1)
- `lsi11_poll_devices` — device polling (core 0)
- `core_poll_irq` — interrupt polling from within `core_step` (core 1)

## Features

- Two separate firmware images:
  - `pico_lsi11.uf2` — `lsi11` profile, built with `ENABLE_MMU=0`
  - `pico_pdp1184.uf2` — `pdp11/84` profile (RAM 128 KB), built with `ENABLE_MMU=1`
- SD card via `SPI0`:
  - `MISO` = `GP16`
  - `CS` = `GP17`
  - `SCK` = `GP18`
  - `MOSI` = `GP19`
- Text I/O via `stdio usb`
- On startup, the firmware presents interactive menus for:
  - RP2040 frequency (overclock) selection with voltage profile
  - Machine mode selection (`lsi11` or `pdp11/84`) unless fixed at build time
  - CPU model selection (`dcj11`, `k1801vm1`, `k1801vm2`)
  - Numbered disk image list from SD card
  - Image type selection (`RK`, `RH`, `RL`)
  - Boot trace mode (`off` / `on`)

## Build

```bash
cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build
```

If `PICO_SDK_PATH` is already set in the environment, `-DPICO_SDK_PATH=...` can be omitted.

Generated firmware files:

- `build/pico_lsi11.uf2`
- `build/pico_pdp1184.uf2`
