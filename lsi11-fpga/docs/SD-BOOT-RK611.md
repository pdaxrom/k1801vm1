# SD bootstrap and RK611 service

## Hardware registers

| Address | Access | Meaning |
|---|---|---|
| `177500` | read/write | transfer one SPI byte; reads transmit `FF` |
| `177502` | read/write | bit 0 CS high, bit 1 fast clock, bit 2 remove overlay |

The SD divisors remain unchanged at 68 for initialization and 2 for transfers.
At the 29.56 MHz board clock this produces approximately 217.35 kHz and
7.39 MHz respectively; no SD protocol or divider change is part of the FRAM
performance work.

## Reset bootstrap

`sd_boot.asm` is ordinary DCJ-11 assembly built by `microasm11`. It starts at
`004000` and is packed into MicROM spare bits. Its sequence is:

1. Initialize SP and diagnostics at `157774/157776`.
2. Wait a 16-bit SOB sweep for SD power settling.
3. Supply at least 80 clocks with CS high.
4. Send `CMD0`, require idle R1 `01`, then end the transaction.
5. Send `CMD8` with argument `000001AA` and CRC `87`; verify R7 echo.
6. Loop `CMD55` and `ACMD41(HCS)` with a transaction boundary between them.
7. Send `CMD58`; require OCR power-up and CCS, deliberately rejecting SDSC.
8. Switch to fast clock and read LBA 0 and 1 with `CMD17` into FRAM.
9. Establish the two-sector ABI, arm overlay removal and execute `CLR PC`.

Command and data-token loops are bounded. `close_card` holds CS briefly,
raises it and provides trailing clocks. This matches the hardware-proven
Stable J11 path; some cards stop answering after commands under continuous CS.

On failure R0 and stage are stored in diagnostics and CPU halts into ODT. The
bootstrap never writes the card.

## RK611 service

Writable registers at `177440..177476` live in private FRAM bank 1. RTL
supplies fixed RKDS and initial/DONE values, recognizes clear, NOP, PACK ACK,
READ and WRITE, and handles interrupt sequencing.

READ/WRITE enter `rk_service.asm` at private vector `160000`. The service:

1. saves R0-R5 and reads WC, BA, DA, DC;
2. converts RK05 geometry (22 sectors, 3 heads) to linear SD LBA;
3. sends `CMD17` or `CMD24`;
4. transfers a 512-byte sector;
5. updates WC, BA, DA, DC and LBA;
6. repeats until WC is zero;
7. sets DONE/error and returns through fixed RTI `160476`.

READ waits for token `FE`, copies the requested words, discards a partial
sector remainder and both CRC bytes, then closes the transaction.

## WRITE fixes

The service sends `FE`, uses MicROM-fixed `MOVB (R2)+,(R4)` for 512 bytes, and
sends two `FF` CRC bytes. The earlier register-mediated software workaround is
not used.

Only the low five bits of an SD data-response token are defined. A physical
card may return `E5`, not only `05`. The service rejects the CRC/write-error
class via status bit 3 and clears bits 7:3, normalizing accepted `E5`. The SD
model intentionally returns `E5`, permanently covering this bug.

That full-byte comparison was one fault that let RT-11 print its banner and
then stop before the prompt. A single short fixed hold was also marginal on
the physical card. The compact service now performs up to six full 16-bit
no-clock intervals, sampling busy once between intervals. This is bounded and
does not flood the card with SPI clocks.  A one-word `NOP` after this loop is
an intentional EBR spare-bit packing spacer: without it the clean Diamond Map
requires 643/640 slices. A future expansion should use a two-second timer and
`CMD13` like Stable J11, but must preserve the 640-slice and firmware-store
limits.
