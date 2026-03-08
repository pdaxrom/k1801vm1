# pico-lsi11

Pico port of the LSI-11 / PDP-11 emulator for Raspberry Pi Pico and Pico 2,
based on sources from `../lsi11` and `../core`.

## Architecture

The emulator takes advantage of the dual-core RP2xxx design:

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
  - `pico_pdp1184.uf2` — `pdp11/84` profile (`128 KB` RAM on Pico, `384 KB`
    on Pico 2), built with `ENABLE_MMU=1`
- SD card via `SPI0`:
  - `MISO` = `GP16`
  - `CS` = `GP17`
  - `SCK` = `GP18`
  - `MOSI` = `GP19`
- Text I/O via `stdio usb`
- Optional mirrored terminal output via `-display`
  - compile-time backend: `st7565`, `ili9486l`, or `none`
- Startup config can come from either:
  - `0:/default.conf`, which is loaded automatically if present
  - another root-level `*.conf`, selectable after the `default.conf` check
  - the built-in interactive menu if no config file is selected
- The interactive menu supports:
  - RP2040 frequency (overclock) selection with voltage profile
  - CPU model selection (`dcj11`, `k1801vm1`, `k1801vm2`)
  - `FIS` and `FP11` overrides with `skip` / `force enable` / `force disable`
  - multi-unit attachment for `RK`, `RH`, `XP/RP`, `RL`, and `TQ`
  - boot-device selection from attached `rkN`, `rhN`, `rlN`, or `tqN`
  - display enable toggle

## Configuration

`pico-lsi11` reuses the same option-file syntax as `../lsi11/options.c`, including
`@nested.conf` references. `default.conf` is auto-loaded first if present, and then
other root-level `*.conf` files can be selected before falling back to the menu.

Example `0:/default.conf`:

```text
-freq 250
-cpu k1801vm2
-display
-rl 0:/bsd-root.rl02
-xp 0:/bsd-usr.rm05
-boot rl0
```

Useful Pico-specific notes:

- `-display` enables mirrored terminal output on the display backend compiled into the firmware.
- `-no-display` forces the display mirror off.
- `-freq <mhz>` sets the RP2040 system clock from config (`125`, `133`, `150`,
  `166`, `180`, `200`, `225`, `250`, `266`, `280`, `300`).
- `pico_pdp1184` defaults to `128 KB` RAM on Pico and `384 KB` on Pico 2, and
  rejects larger `-ram` values for the selected board.
- `-dz` is not supported on the Pico target.

## Build

Build for the original Pico (`RP2040`):

```bash
cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build
```

Build for Pico 2 (`RP2350`):

```bash
cmake -S . -B build-pico2 -DPICO_BOARD=pico2 -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build-pico2
```

If `PICO_SDK_PATH` is already set in the environment, `-DPICO_SDK_PATH=...` can be
omitted.

Display backend selection at configure time:

```bash
cmake -S . -B build -DPICO_LSI11_DISPLAY_BACKEND=none
cmake -S . -B build -DPICO_LSI11_DISPLAY_BACKEND=st7565
cmake -S . -B build -DPICO_LSI11_DISPLAY_BACKEND=ili9486l
```

Backends:

- `st7565` uses the legacy 128x64 mirrored text display.
- `ili9486l` links `ili9486l_lcd` as a library and mirrors output into its VT100 terminal.
- `none` builds firmware without any display support; `-display` then becomes a configuration error.

Generated firmware files:

- `build/pico_lsi11.uf2`
- `build/pico_pdp1184.uf2`
- `build-pico2/pico_lsi11.uf2`
- `build-pico2/pico_pdp1184.uf2`
