# FRAM and the existing HC1200 peripherals

Audit: 2026-09-08. Source paths below are relative to sibling `lsi11-fpga`;
SHA-256 values identify the inspected working files in `fram-source-audit.json`.
That working tree contains ongoing changes; this experiment does not modify it.

## Memory is serial FRAM

The board uses MR45V100A, 128 KiB × 8 SPI FRAM. The oscillator in
`boards/hc1200-microcomp/am4_microcomp.v` is **29.56 MHz**; `FRAM_CLK_DIV=1`
gives **14.78 MHz SCK**. The historical CP9 core's 40 MHz internal timing result did not establish
that the existing board runs at 40 MHz. CP12 core passes 35 MHz; its
FRAM/prefetch probe passes 29.56 MHz (TRACE 31.287 MHz).

`rtl/spi_fram_guest_ram.v` implements mode 0, READ=03, WREN=06, WRITE=02.
Each word read opens a new CS transaction: 8 command bits, 24 address bits,
16 data bits, **48 SCK pulses**. A word write first sends WREN under its own
CS, then WRITE/address/data: **56 pulses in two transactions**. Bytes take
40 read / 48 write pulses. Data is little endian; a byte uses its exact address
and the low eight data bits. Odd words return an error without touching SPI.
`request_seen` requires one sampled low request between accepted operations.
The uJ11 baseline adapter supplies this on the ready edge for consecutive beats.

The SPI address header width is a device protocol requirement. uJ11 always
sends zero in its high address byte; CPU addresses remain exactly 16 bits.
AM4's private FRAM bank for RK bookkeeping is peripheral storage, not a uJ11
processor bank or address extension.

The manufacturer datasheet specifies continuous READ with auto-increment while
CS remains low (p. 7), and a DC-to-34 MHz READ clock range (p. 15). Thus SCK
can pause between words. CS setup, deselect and active hold minima are 10 ns.
The experiment finishes an outstanding read word before discarding it or
closing CS; it does not terminate ordinary reads midway through a byte.
Source: LAPIS **FEDR45V100A-01**, 2017-09-04,
[manufacturer PDF, Mouser mirror](https://www.mouser.com/datasheet/2/348/FEDR45V100A-01-1280312.pdf).
Pin timing and oscillator tolerance still need a board-level timing check;
internal register-to-register Fmax is a separate measurement.

## Address map and side effects

All addresses below are octal. The implementation is in
`boards/hc1200-microcomp/am4_cpu11_bus.v`, `rtl/spi_byte_service.v` and
`rtl/experimental/lsi11/wbc_uart_xo2.v`.

| Address | Device | Semantics relevant to uJ11 |
|---|---|---|
| 000000..157777 | Main FRAM | 16-bit CPU address; final aligned word is 157776 |
| 160000..177777 | I/O page | Never fall through to FRAM on an unknown CSR |
| 166000/166001 | Panel | Low byte reads key rows in bits 7:4; high byte is output latch, reset 12 hex |
| 177440..177476 | RK611 service | Register subset, private FRAM backing and microcode ROM service; not a hardware disk DMA engine |
| 177500 | SD SPI data | **Both reads and writes clock one byte**; reads transmit FF |
| 177502 | SD control | CS high bit 0, fast clock bit 1, boot-overlay release arm bit 2 |
| 177546 | KW11-L | DONE/MON bit 7, IE bit 6, 50 Hz, event vector 100 |
| 177560..177566 | KL11 | RCSR/RBUF/XCSR/XBUF, 115200 8N1; vectors 060 RX and 064 TX |

RXBUF acceptance clears the receive-full/overflow/interrupt state. UART
high-byte writes acknowledge without changing low-byte CSR or TX data.
The timer resets DONE=1, IE=0; low-byte writes set IE and may clear DONE.
Panel low-byte writes have no output effect. SD has a separate SPI pin set;
it cannot be treated as a plain cached register. Panel display/key scanning
is software-driven; there is no FPGA framebuffer/font storage to copy.

AM4 bus lanes use byte selects and positioned high-byte data. uJ11's byte
interface is right-justified; a future shared peripheral adapter must shift
odd-byte writes/reads at that boundary, preserving the exact CSR address.

AM4 boot overlays supply vectors 024/026, 100..106 and the SD loader around
004000..004777. SD control arms removal, then the first read at zero removes
the overlay. RK service executes ROM in the I/O page and distinguishes service
instruction/data accesses. None of these overlays is active in the CP6 uJ11
test system. Porting them requires an explicit mapping/stream qualification
before speculative reads; any mapping change must invalidate buffered data.

## Consequences for CP6

`uj11_fram_system` connects M0 to FRAM and exposes the whole I/O page as a
**demand-only** port. It is not yet the complete HC1200 board top. No UART,
timer, SD, panel, boot overlay, RK service or interrupt logic is silently
included in a reported core+FRAM resource count.

Prefetch must represent the next stream word, eventually including immediate,
absolute and displacement words. Only ordinary FRAM may be read speculatively.
PC redirects, data accesses and writes discard the prediction; a pending
speculative read completes without architectural ACK if discarded. Data writes
invalidate even a matching buffered instruction (self-modifying code).
No speculative I/O request, vector fetch or peripheral error is committed.

A one-word buffer alone hides only a few CPU clocks. The first measured legacy
baseline is **107 microclocks/instruction** on all five M0 loops: 512 retired
instructions, 54,784 CPU clocks, 512 CS transactions and 24,576 SCK rises.
At nominal 29.56 MHz this is **276,262 instructions/s** in simulation-derived
throughput, not an on-board benchmark. The earlier two-clock ideal-RAM figure
describes the execution core, not this memory system.

Compare three configurations with the same CPU and program: the unchanged
legacy controller; a retained sequential READ; sequential READ with one-word
prefetch. Count CPU clocks, accepted memory beats, CS transactions and SPI
clocks independently. Retain the buffer only with a measured resource cost.

Prior AM4 transparent sequential-access experiments failed to fit: the local
`docs/PORTING-NOTES.md` records at least **674/640 slices**, and **672/640** for
read-only, versus the buildable one-transaction-per-access implementation.
These are historical board totals from that document, not new uJ11 estimates.
This is why CP6 has its own synthesis gates before adding addressing modes.


## CP10 byte stream and redirect

Aligned byte instruction-stream reads consume a complete internal FRAM word,
return its low byte, and advance the predicted word address by two. Ordinary
byte operands still transfer one byte; peripheral accesses remain demand-only.
A registered PC mismatch masks prediction and buffer validity immediately
after the PC-write edge, then clears their stored state on the following edge.
Matching PC writes retain the buffer. This adds one FF without changing the
430 pre-existing benchmark cycle counts. Final scope: 905 LUT/394 FF/4 EBR,
29.56 MHz PASS. Full peripheral integration and board timing remain separate.

## CP20: instruction RESET

Новый выход `uj11_fram_system.peripheral_reset` подключается к одноимённому
входу frozen peripheral bus, а также к reset IRQ resolver через OR с board reset.
Production JUMP.init фиксирует импульс в выходном регистре на следующий
settling clock, затем происходит retirement. Async reset UART не подключён
напрямую к комбинационному decode ROM. CPU/RF/PSW и uJ11 FRAM transport этот сигнал не сбрасывает.
Проверены pending KL11/KW11, panel/SD/RK control reset, сохранение FRAM READ/WRITE
и новый IRQ после RESET. Для manual RK CSR используется отдельная модель
private peripheral FRAM; full RK ROM service/DMA ещё не интегрированы.
[Контракт и verification](system-control.md).
