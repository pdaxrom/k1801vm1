# MK-90 Hardware Notes

This note combines:

- the reverse-engineered hardware description in `docs/mkonly/mk90hwe.htm`
- the SMP cartridge notes in `docs/mkonly/mk90came.htm`
- the legacy Pascal emulator in `docs/mkonly/mk90emsrc`
- the current `mk90/` implementation in this repository

Values are octal by default. Hex is shown in parentheses only where it helps
cross-reference the original materials.

## CPU And Reset

- The machine is a `VM2`-class PDP-11/K1801 system with MK-90-specific glue
  logic and peripherals.
- The reset entry point is `0173000` (`0xF600`).
- The fixed low-RAM size used by the current emulator is `040000` bytes
  (16 KiB), matching the normal MK-90 configuration.
- The emulator keeps room for up to `0100000` bytes of RAM for experiments,
  but the default machine configuration is still 16 KiB.

On reset, the current emulator does the following:

- resets `KA1835VG3` LCD state
- resets keyboard state
- resets `KA1835VG5` system-controller registers `RG1` and `RG2`
- resets SMP serial state
- resets `KA1835VG4` I/O controller state
- resets `KA512VI1` RTC on cold power-up
- sets `SEL0 = 0173000`

At power-up the firmware starts in the fixed high-ROM window, then programs
`RG1/RG2` early in boot to expose the rest of the ROM.

## Interrupt Vectors

The documented vectors used by the MK-90 firmware are:

| Vector | Hex | Source |
| --- | --- | --- |
| `000100` | `0x0040` | `EVNT`, driven from RTC square-wave output |
| `000300` | `0x00C0` | `INRT`, used by RTC IRQ through the I/O controller |
| `000304` | `0x00C4` | data-register-ready interrupt from `KA1835VG4` |
| `000310` | `0x00C8` | keyboard / external interrupt |

The current emulator polls them in the order `EVNT`, `INRT`, `DATA_READY`,
`KEYBOARD`.

## ROM Images

The legacy emulator and the current `mk90/` target use two ROM files:

| File | Backing-store range | Size | Purpose |
| --- | --- | --- | --- |
| `romt.bin` | `040000-077777` | `040000` bytes | optional test ROM |
| `rom.bin` | `0100000-0177377` | `0077400` bytes | main firmware ROM |

These ranges describe how the images are loaded into the emulator's ROM backing
array. CPU-visible access is still controlled by the `KA1835VG5` decoder.

## Address Space Overview

The MK-90 has a 64 KiB CPU address space (`000000-0177777`), but the visible
memory map is dynamic because `KA1835VG5` decides whether a given address is
served by RAM or ROM.

Fixed regions:

| CPU range | Hex | Function |
| --- | --- | --- |
| `000000-037777` | `0x0000-0x3FFF` | physical RAM in the normal 16 KiB machine |
| `0164000-0165777` | `0xE800-0xEBFF` | I/O page |
| `0166000-0176777` | `0xEC00-0xFDFF` | fixed high-ROM window |

Important note:

- The current ROM backing store extends above `0176777`, because that is how
  the legacy emulator loaded `rom.bin`.
- The decoder model currently exposes the documented ROM windows used by the
  firmware, not the whole backing-store tail.

## KA1835VG5 System Controller

Registers:

| Address | Hex | Register | Size |
| --- | --- | --- | --- |
| `0164032` | `0xE81A` | `RG1` | word |
| `0164034` | `0xE81C` | `RG2` | word |

The current emulator uses the same practical model as the old Pascal emulator.

### RAM Write Window

Writes to low memory use `RG1` bits `10:9`:

| `RG1[10:9]` | CPU range accepted as RAM write |
| --- | --- |
| `0` | `000000-0157777` |
| `1` | `000000-0077777` |
| `2` | `000000-0037777` |
| `3` | `000000-0017777` |

The I/O page `0164000-0165777` is always write-enabled regardless of this
window.

### RAM/ROM Read Window

Reads use `RG1` bits `13:11`:

| `RG1[13:11]` | Low RAM visible | Low ROM visible |
| --- | --- | --- |
| `0` | `000000-0157777` | none |
| `1` | `000000-0077777` | `0100000-0157777` |
| `2` | `000000-0037777` | `040000-0157777` |
| `3` | `000000-0017777` | `020000-0157777` |
| `4-7` | none | `000000-0157777` |

Again, the I/O page stays readable independently of these windows.

### `RG2` Bits Currently Modeled

The current model uses two `RG2` controls:

| Bit mask | Effect |
| --- | --- |
| `0020000` | disables ROM reads entirely |
| `0001000` | exposes an extra ROM window at `0160000-0163777` |

ROM reads also always include the fixed high-ROM window `0166000-0176777` when
ROM is enabled.

## I/O Page

### KA1835VG3 LCD And Memory Controller

Registers:

| Address | Hex | Register | Size | Notes |
| --- | --- | --- | --- | --- |
| `0164000` | `0xE800` | display base address | word | start of video RAM |
| `0164002` | `0xE802` | configuration | word | firmware initializes it to `0104306` (`0x88C6`) |
| `0164004` | `0xE804` | low byte alias | byte | legacy documentation |
| `0164006` | `0xE806` | low byte alias | byte | legacy documentation |

Display characteristics used by the emulator:

- monochrome LCD
- `120 x 64` pixels
- framebuffer size `960` bytes
- framebuffer lives in normal RAM; the LCD controller only stores its base
  address and configuration

Current render layout:

- rows `0-31` use bytes at `base + 0, +2, +4, ...`
- rows `32-63` use bytes at `base + 1, +3, +5, ...`
- each row consumes 15 bytes = 120 pixels

### KA1835VG4 Serial / I/O Controller

Registers:

| Address | Hex | Register | Size |
| --- | --- | --- | --- |
| `0164020` | `0xE810` | data | byte |
| `0164022` | `0xE812` | transfer rate / `RGRQ` readback | word |
| `0164024` | `0xE814` | control / status | byte |
| `0164026` | `0xE816` | command | byte |

Documented channel usage:

| Channel | Device |
| --- | --- |
| `0` | SMP slot 0 |
| `1` | SMP slot 1 |
| `2` | keyboard controller `KA1835VG1` |
| `3` | piezo buzzer clock |

Control-register bits from the hardware notes:

- bits `2:0`: channel select
- bit `3`: transfer direction, `0=input`, `1=output`
- bit `4`: external interrupt enable (`0` = enabled), vector `000310`
- bit `5`: data-register interrupt enable (`0` = enabled), vector `000304`
- bit `6`: `INRT` interrupt enable (`0` = enabled), vector `000300`
- bit `7`: transfer mode, `0=slave`, `1=master`

Current emulator behavior:

- channel `0/1` talk to SMP images
- channel `2` returns the latched keyboard scan code
- channel `3` only tracks a beeper level; no audio output yet
- serial timing is simplified; the transfer-rate register is not cycle-accurate

### KA1835VG1 Keyboard Controller

The keyboard controller returns a scan code through channel `2` of the serial
controller. Pressing a key raises the external interrupt at vector `000310`.

The current emulator models this as a latched scan-code word:

- key press stores the scan code
- key release clears it back to zero
- the I/O controller reads the current scan code through channel `2`

### KA512VI1 Real-Time Clock

Address range:

| CPU range | Hex |
| --- | --- |
| `0165000-0165176` | `0xEA00-0xEA7E` |

The `KA512VI1` is equivalent to `MC146818`, with an MK-90-specific bus hookup:

- RTC registers sit on even CPU addresses only
- data is shifted by one bit because RTC `AD0-AD7` are wired to system-bus
  `AD1-AD8`
- writes therefore store `(value >> 1)`
- reads return `(register << 1)`

Interrupt paths:

- square-wave output drives `EVNT` (`000100`)
- RTC `IRQ` drives `INRT` (`000300`) through the I/O controller

The current emulator models:

- time/date registers
- alarm/update/periodic flags
- register `C` clear-on-read semantics
- register `D` valid-RAM bit
- software-driven periodic timing instead of exact crystal/divider timing

## SMP Cartridges

Slots:

- SMP0 is connected to serial channel `0`
- SMP1 is connected to serial channel `1`

The current emulator loads cartridge images from files and keeps per-slot state:

- current command byte
- current address pointer
- image size
- address mask

Practical command groups currently modeled:

| Command group | Meaning in the current emulator |
| --- | --- |
| `0000` | probe / presence check |
| `0240` | load address a byte at a time |
| `0020`, `0320` | read with auto-decrement / auto-increment |
| `0040`, `0300`, `0340` | write with auto-increment / auto-decrement |

Persistence rules in the current emulator:

- images smaller than `0100000` bytes are treated as writable RAM cartridges
- images `>= 0100000` bytes are treated as ROM cartridges
- dirty RAM-cartridge images are written back to the source file on close

### SMP Media Layout

For BASIC V1.0 cartridges, `docs/mkonly/mk90came.htm` describes this layout:

| Cartridge range | Hex | Meaning |
| --- | --- | --- |
| `000000-001777` | `0x0000-0x03FF` | loader |
| `002000-003777` | `0x0400-0x07FF` | directory |
| `004000-023777` | `0x0800-0x27FF` | data area |

The start menu options `SMP0` and `SMP1` execute cartridge loader code from the
selected slot.

## Current Emulator Coverage

Already modeled:

- boot from `0173000`
- `KA1835VG5` address decoding at the level used by the legacy emulator
- LCD base/config registers and framebuffer rendering
- RTC register set and interrupt paths
- keyboard scan-code latch and `000310` interrupt
- SMP slot access over the `KA1835VG4` channels
- deterministic headless boot/test/smoke scenarios

Still approximate or incomplete:

- exact `KA1835VG4` serial timing
- exact `KA1835VG5` bit naming and all undocumented modes
- beeper audio generation
- any analog/power-management behavior

## Reference Files

- `docs/mkonly/mk90hwe.htm`
- `docs/mkonly/mk90came.htm`
- `docs/mkonly/1835vg5.htm`
- `docs/mkonly/mk90emsrc/def.pas`
- `docs/mkonly/mk90emsrc/iosystem.pas`
- `docs/mkonly/mk90emsrc/syscon.pas`
- `docs/mkonly/mk90emsrc/rtc.pas`
