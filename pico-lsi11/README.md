# pico-lsi11

Pico port of the emulator based on sources from `../lsi11` and `../core`.

## Implemented

- Two separate firmware images:
  - `pico_lsi11.uf2` - `lsi11` profile, build with `ENABLE_MMU=0`
  - `pico_pdp1184.uf2` - `pdp11/84` profile (RAM 128 KB), build with `ENABLE_MMU=1`
- SD card via `SPI0`:
  - `MISO` = `GP16`
  - `CS` = `GP17`
  - `SCK` = `GP18`
  - `MOSI` = `GP19`
- Text I/O via `stdio usb`.
- On startup, the firmware shows:
  - RP2040 frequency (overclock) selection with voltage profile
  - machine mode selection (`lsi11` or `pdp11/84`) unless fixed at build time
  - CPU model selection
  - numbered disk image list from SD card
  - image type selection (`RK`, `RH`, `RL`)
  - boot trace mode (`off`/`on`)

## Build

```bash
cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build
```

If `PICO_SDK_PATH` is already set in the environment, `-DPICO_SDK_PATH=...` can be omitted.

Generated firmware files:

- `build/pico_lsi11.uf2`
- `build/pico_pdp1184.uf2`
