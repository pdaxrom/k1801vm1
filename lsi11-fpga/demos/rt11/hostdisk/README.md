# HG host directory disk for RT-11

`HG.MAC` is an RT-11 V5 block-device handler for the four FT2232/JTAG pins on
the HC1200 AM4 board. It supports the unmapped RT-11SJ and RT-11FB monitors.
The current handler is synchronous: an HG transfer occupies the CPU until the
host finishes it. `HG0:` is a data disk, not a boot device.

`HG.SYS` was assembled and linked by the distributed RT-11 V5.03 tools. The
source is included so the handler can also be rebuilt on the target system.
It carries `H.GEN=5`, matching the distributed unmapped `RT11SJ` and `RT11FB`
monitors (`ERL$G` and `TIM$IT`). A handler built with the default `H.GEN=0`
is rejected by `INSTALL` with `Conflicting SYSGEN options`.

The link uses the pins only while the physical `JTAG_EN` jumper selects GPIO:

| FT2232 A pin | JTAG name | HG function |
|---:|---|---|
| ADBUS0 | TCK | host clock |
| ADBUS1 | TDI | host-to-RT-11 data |
| ADBUS2 | TDO | RT-11-to-host data/request |
| ADBUS3 | TMS | host select |

TDO is an FPGA output only while panel-register bit 15 is set. The handler
first shifts `0xff` into the RGB/keyboard output register, releasing every
keyboard column, and only then enables TDO. The keyboard and HG disk therefore
must not be used concurrently.

## Build the host daemon

On macOS with Homebrew `libftdi` installed:

```sh
cd host/hg
make test
make
```

Serve a native RT-11 disk image:

```sh
./hgfsd --image volume.dsk
```

Or expose a host directory through an RT-11 image mirror:

```sh
mkdir -p shared
./hgfsd --directory shared
```

The directory mode creates `shared/.hg-volume.dsk` on first use and imports
regular files whose names already fit RT-11 6.3 RAD50 syntax. RT-11 writes are
exported back as uppercase host files after a short idle period. RT-11 files
have block-granular lengths, so exported files can contain zero padding through
the end of their last 512-byte block. Host-side edits are imported when the
daemon is restarted. Deleting a file in RT-11 does not delete the corresponding
host file in this initial implementation.

The default MPSSE clock is 4 kHz, verified for both reads and writes on the
HC1200/AM4 target. The RT-11 handler bit-bangs this link in software, so faster
symmetric TCK periods can overrun its per-bit GPIO loop. The host clocks one
byte per MPSSE command, leaving TCK low while the handler prepares the next
byte. Use `--clock 1000` as a conservative fallback for a marginal link;
frequencies above 4 kHz should be validated read-only before enabling writes.

## Assemble and install HG.SYS under RT-11

Copy `HG.MAC` to the RT-11 system disk as CRLF text. At the RT-11 prompt:

```text
.MACRO HG
.LINK/NOBITMAP/EXECUTE:HG.SYS HG
.INSTALL HG
.LOAD HG
.DIRECTORY HG:
```

On systems without the MACRO and LINK command-language shortcuts, the exact
MACRO utility command string is:

```text
.R MACRO
*HG.OBJ,HG.LST=HG.MAC
*^C
```

Use the monitor `LINK/NOBITMAP/EXECUTE:HG.SYS HG` command shown above for the
link step. Do not substitute `HG.SYS/B:0=HG.OBJ` at the interactive LINK
prompt: that produces a two-block file and truncates the resident handler,
which then crashes during `LOAD HG`.

If `SHOW DEVICE` reports no free handler slot, remove an unused device before
`INSTALL HG` and reinstall it afterward if needed.

## Jumper sequence

For programming the FPGA, stop `hgfsd`, move `JTAG_EN` to JTAG, and program the
board normally. For HG disk operation, return `JTAG_EN` to GPIO before starting
`hgfsd`. Closing the daemon resets FT2232 channel A to high impedance.

Do not move `JTAG_EN` while either the programmer or `hgfsd` owns channel A.
