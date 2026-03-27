# mk90

SDL front-end and machine adapter for the Elektronika MK-90, built on top of
the shared PDP-11/K1801 `core/`.

See `HARDWARE.md` for the MK-90 address map, interrupt vectors, peripheral
registers, ROM layout, and notes on what the current emulator models.

## Layout

- `main.c`: SDL event loop, CLI, CPU stepping.
- `mk90_machine.*`: core integration, image loading, IRQ polling, memory map.
- `mk90_lcd.*`: LCD controller registers and framebuffer rendering.
- `mk90_rtc.*`: `KA512VI1` / `MC146818`-compatible RTC model.
- `mk90_io.*`: `KA1835VG4` serial/I/O controller.
- `mk90_syscon.*`: `KA1835VG5` address-space controller.
- `mk90_smp.*`: removable SMP cartridge images.
- `mk90_keyboard.*`: latched keyboard scan code state.
- `roms/`: local ROM images used by the emulator at runtime.
- `media/`: local SMP cartridge images used by the emulator at runtime.
- `docs/mkonly/`: reference materials only; runtime image loading does not depend on it.

## Number Style

- CPU-visible addresses, vectors, register values, and machine words use octal.
- Hex is used only at documentation boundaries in comments when it helps cross-
  reference the MK-90 materials.

## Build

```sh
make -C mk90
```

The current binary is produced as `.build/mk90`.

## Run

```sh
./mk90/.build/mk90
```

Default ROM image paths are taken from `roms/`.
Default SMP image paths are taken from `media/`.

Expected files:

- `roms/rom.bin`
- `roms/romt.bin`
- `media/smp0.bin`
- `media/smp1.bin`

Optional bootable SMP:

- `media/trex.bin` from [azya52/MK90](https://github.com/azya52/MK90)

Useful options:

- `--headless`
- `--frames <n>`
- `--steps-per-frame <n>`
- `--trace`
- `--tick-ms <n>` (fixed RTC/frame time step for deterministic headless runs)
- `--dump-pgm <path>`
- `--tap-key <octal>`
- `--tap-frame <n>`
- `--tap <frame>:<octal>` (repeatable scripted key taps for headless runs)
- `--type <text>` (repeatable headless host-key input; supports `\n`, `\r`, `\b`, `\\`)
- `--type-frame <n>`
- `--type-step <n>`
- `--rom <path>`
- `--romt <path>`
- `--smp0 <path>`
- `--smp1 <path>`

Example: enter the test ROM, type test number `1`, and press Enter:

```sh
mk90/.build/mk90 --headless --tick-ms 16 --frames 260 \
  --tap 210:123 --type-frame 220 --type '1\n'
```

## Smoke

```sh
make -C mk90 smoke
```

The smoke run exercises the current boot menu, BASIC entry, test menu entry,
ROMT test-number input, and SMP menu paths via headless scripted key taps. If
`media/trex.bin` is present, smoke also verifies the bootable SMP path through
that image. The non-bootable SMP check is pinned to `media/smp1.bin`, so
replacing `media/smp0.bin` does not destabilize smoke baselines. Smoke uses a
fixed `--tick-ms 16` step so results do not depend on host timing.
