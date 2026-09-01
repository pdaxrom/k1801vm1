# RT-11 HC1200 panel demos

These programs exercise the private HC1200 panel register at `166000/166001`
from a normal RT-11 foreground job. The ready-to-run files were assembled by
RT-11 V5.03 `MACRO.SAV` and linked by its `LINK.SAV` against `PNLDRV.OBJ`.

| Program | Demonstration |
|---|---|
| `DSPDEM.SAV` | Cycles four 16-character messages on the two HCMS-3917 displays. |
| `RGBDEM.SAV` | Cycles red, green, blue, yellow, magenta, cyan, white and off. |
| `KEYDEM.SAV` | Scans all 20 panel keys, shows raw codes `00` through `13`, and changes the RGB color for each accepted key. |

All three programs stop when the physical panel ESC key is pressed. They turn
the RGB LED off and leave a final status message on the display before returning
to the RT-11 monitor.

## Put the demos on a copy of an RT-11 image

From `lsi11-fpga`:

```sh
mkdir -p build
cp images/rt11v503.dsk build/rt11-panel-demo.dsk
../lsi11/rt11tool add build/rt11-panel-demo.dsk demos/rt11/panel/DSPDEM.SAV DSPDEM.SAV
../lsi11/rt11tool add build/rt11-panel-demo.dsk demos/rt11/panel/RGBDEM.SAV RGBDEM.SAV
../lsi11/rt11tool add build/rt11-panel-demo.dsk demos/rt11/panel/KEYDEM.SAV KEYDEM.SAV
```

The checked-in `images/rt11v503.dsk` is not modified by these commands. Boot
the copied image and run a demo at the RT-11 prompt:

```text
.RUN DSPDEM
.RUN RGBDEM
.RUN KEYDEM
```

The programs require the AM4 HC1200 FPGA panel CSR. A generic PDP-11 emulator
without that machine-specific register cannot execute the hardware accesses.

## Driver details

`PNLDRV.MAC` is shared by the three applications. It exports:

- `PNINIT` to initialise and clear the panel;
- `PNDISP` to send one fixed 16-character buffer;
- `PNRGB` to set an active-high color mask (`R=1`, `G=2`, `B=4`);
- `PNKEY` to return a raw key code from `0` through `19`, or `-1`;
- `PNWAIT` to wait in 20 ms RT-11 line-clock ticks.

The implementation follows the original HC1200
`microcpu/asm/modules/display.asm`: HCMS control byte `0x4c` is sent eight
times, display data is shifted MSB first, the downstream eight characters are
sent first, and the same 20-entry key map is used. The display is held blank
until all 80 dot-column bytes have been loaded. The 5-by-7 font covers ASCII
space through underscore, which is enough for the uppercase demo text.

## Rebuild with RT-11 tools

Copy `PNLDRV.MAC`, `DSPDEM.MAC`, `RGBDEM.MAC`, and `KEYDEM.MAC` to an RT-11
disk as CRLF text files, then run:

```text
.MACRO PNLDRV
.MACRO DSPDEM
.MACRO RGBDEM
.MACRO KEYDEM
.LINK DSPDEM,PNLDRV
.LINK RGBDEM,PNLDRV
.LINK KEYDEM,PNLDRV
```

The source uses six-character global and file names so it remains compatible
with the V5.03 toolchain.
