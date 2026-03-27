# mk90

SDL front-end and machine adapter for the Elektronika MK-90, built on top of
the shared PDP-11/K1801 `core/`.

See `HARDWARE.md` for the MK-90 address map, interrupt vectors, peripheral
registers, ROM layout, and notes on what the current emulator models. Open
bring-up items live in `TODO.md`.

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

Reference ROM source texts:

- `roms/src/rom.src`
- `roms/src/romt.src`

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
- `--tap-hold <n>`
- `--tap <frame>:<octal>[:<hold>]` (repeatable scripted key taps for headless runs)
- `--type <text>` (repeatable headless host-key input; supports `\n`, `\r`, `\b`, `\\`)
- `--type-frame <n>`
- `--type-step <n>`
- `--type-hold <n>`
- `--rom <path>`
- `--romt <path>`
- `--smp0 <path>`
- `--smp1 <path>`

Example: enter the test ROM, type test number `1`, and press Enter:

```sh
mk90/.build/mk90 --headless --tick-ms 16 --frames 260 \
  --tap 210:123 --type-frame 220 --type '1\n'
```

## SDL Keyboard

The SDL front-end now follows a normal PC physical keyboard layout based on
`SDL_Scancode`, so the mapping no longer depends on the current host text
layout.

- printable keys are remapped to ordinary PC positions:
  - top row: `` ` 1 2 3 4 5 6 7 8 9 0 - = ``
  - letter rows: `QWERTYUIOP[]\\`, `ASDFGHJKL;'`, `ZXCVBNM,./`
  - punctuation coverage keeps all MK-90 printable scan codes reachable:
    `` ` -> @ ``, `= -> ^`, `; -> :`, `' -> ;`, `/ -> /`
- `Tab`, `F6`, or either host `Ctrl` -> `[SU]`
- cursor keys -> `up`, `left`, `right`, `down`
- `Backspace` or `Delete` -> `[ZV]`
- `Enter` or keypad Enter -> `[VK]`
- `Home` or `F7` -> `[R/L]`
- `End` or `PageDown` -> the right key left of the space bar
- `Space` -> space bar
- `Insert` or `PageUp` -> the left key right of the space bar
- `F8` or either host `Alt` -> `[FK]`
- `F9` or either host `Shift` -> `[V/N]`
- keypad digits `/ - .` are mirrored to the matching MK-90 numeric/punctuation
  scan codes

The SDL key handling also now tracks the currently pressed host key, so focus
loss or releasing a different host key no longer clears the latched MK-90 key
spuriously.

## Smoke

```sh
make -C mk90 smoke
```

The smoke run exercises the current boot menu, BASIC entry, test menu entry,
ROMT test-number input, the raw `T -> 3` ROMT jump to `040000`, ROMT LCD-test
key handling, and SMP menu paths via headless scripted key taps. If
`media/trex.bin` is present, smoke also verifies the bootable SMP path through
that image. The non-bootable SMP check is pinned to `media/smp1.bin`, so
replacing `media/smp0.bin` does not destabilize smoke baselines. Smoke uses a
fixed `--tick-ms 16` step so results do not depend on host timing.
