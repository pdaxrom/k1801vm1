# HC1200 display, RGB and keyboard register

## Why `166000`

The panel occupies one word at `166000`; its odd-byte address is `166001`.
This is the start of the PDP-11 customer/third-party CSR area and does not
overlap the configured RK611, SD, KW11-L or KL11 registers. Address `177750`
is deliberately not used because it belongs to the DCJ11/J-11 MAINT register.
Software should still treat `166000` as machine-specific rather than a DEC
standard device address.

## Register layout

A word read from `166000` returns:

```text
 15        14 13                         8 7                 4 3          0
+------------+----------------------------+-------------------+------------+
|      0     |       output readback      |   keyboard rows   |      0     |
+------------+----------------------------+-------------------+------------+
```

The low byte at `166000` is read-only. Its bits 7:4 reflect physical
`gpio_key_row[3:0]`; bits 3:0 read zero. The high byte at `166001` is a
six-bit read/write output latch:

| Byte bit | Physical output | Use |
|---:|---|---|
| 0 | `gpio_din` | shared serial data |
| 1 | `gpio_ce` | HCMS chip enable, active low |
| 2 | `gpio_clk` | shared serial clock |
| 3 | `gpio_rs` | HCMS register select |
| 4 | `gpio_blank` | HCMS blanking |
| 5 | `gpio_reg_latch` | latch external RGB/keyboard output register |
| 7:6 | none | read zero, writes ignored |

Reset sets the latch to `000022` as a high byte: DIN=0, CE=1, CLK=0, RS=0,
BLANK=1 and REG_LATCH=0. This deselects and blanks the displays while leaving
the shared clock low.

A byte write to `166001` updates all six outputs together. A word write to
`166000` has the same effect through its high byte. Low-byte-only writes do
not change the outputs.

## Two HCMS-3917 displays

The two eight-character HCMS-3917 modules form one daisy chain. Software
selects command or data mode with RS, lowers CE, and shifts each byte
most-significant bit first on DIN. CLK is pulsed high and then low for every
bit. Raising CE completes the transfer. Sixteen five-column characters need
80 data bytes for a full refresh.

BLANK is available independently, so software can prepare the serial stream
while the displays are dark and unblank after the update. There is no
character buffer or font in the FPGA; the program owns the font table,
character-to-column conversion and refresh policy.

## RGB LED and keyboard

DIN and CLK also feed the board's external serial output register. Its latch
is separate from HCMS CE: shift a complete output byte with REG_LATCH low,
then pulse REG_LATCH high and low. The existing HC1200 wiring uses output bits
0..2 for active-low RGB and bits 3..7 for five keyboard columns.

To scan the 4-by-5 keyboard matrix:

1. Shift an output byte containing the desired RGB state and exactly one
   active keyboard-column bit.
2. Pulse REG_LATCH and allow the row inputs to settle.
3. Read `166000`; keyboard rows are bits 7:4 of the returned low byte.
4. Repeat for column bits 3 through 7.

The HCMS and external output register share DIN and CLK. Their select/latch
signals keep inactive devices from accepting a transfer, but interrupt and
foreground code must serialize access to the panel register so their bit
streams cannot interleave.

## Minimal PDP-11 access pattern

The register is intentionally raw. A software driver should maintain a shadow
of the high byte and change pins by writing that complete shadow each time:

```text
; conceptual MACRO-11-style sequence
PANEL  = 166000
PANELO = PANEL+1

; drive DIN=1 while preserving CE/CLK/RS/BLANK/LATCH
        BISB    #1,SHADOW
        MOVB    SHADOW,@#PANELO

; sample the four row inputs
        MOVB    @#PANEL,R0
        ASH     #-4,R0
        BIC     #177760,R0
```

The assembly is illustrative rather than a standalone program. Production
code should wrap pin transitions in routines for shift-byte, HCMS-select and
output-register-latch operations, and should exclude concurrent callers.
