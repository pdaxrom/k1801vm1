# Architecture

## CPU core

The processor is an AM4/LSI-11M implementation built around four blocks:

1. `am4_alu.v` implements the 16-bit Am2901-style datapath, sixteen working
   registers and Q register.
2. `am4_seq.v` implements the ten-bit microaddress sequencer and return stack.
3. `am4_plm.v` maps PDP-11 opcodes to MicROM entry points.
4. `am4_direct.v` ties datapath, sequencer, instruction register, PSW,
   interrupt logic and bus state machine together.

The active external interface is a native request/ready transaction:

```text
AM4                    board bus
bus_request   -------> transaction valid
bus_write     -------> read/write direction
bus_address   -------> 16-bit PDP-11 address
bus_write_data-------> write data
bus_byte_select -----> low/high-byte enables
bus_read_data <------- read data
bus_ready     <------- transaction accepted/completed
```

The CPU holds request, address, direction, byte enables and write data stable
until `bus_ready`. Interrupt vectors use a separate
`vector_request/vector_ready/vector_data` handshake. This separation matters
because the original AM4 Wishbone moderator was deliberately removed.

## Control store

The recovered MicROM is 1024 words by 56 bits. `mc.asm` is assembled by the
included Am2900 meta-assembler into `mc.rom`. On MachXO2-1200 the store is
seven `DP8KC` blocks in 1024 x 9 mode. Seven blocks expose 63 physical bits per
address, leaving seven bits unused above the 56-bit microinstruction.

`make_mcrom_ebr.py` uses those spare bits and the second EBR port for PDP-11
firmware:

- reset bootstrap: three 7-bit fragments per 16-bit word;
- main private RK service: three 7-bit fragments per word;
- service extension: three 5-bit fragments plus a small distributed bit-15
  function.

No additional EBR is required. The final build consumes all seven EBRs.

## Address map

| PDP-11 address | Function |
|---|---|
| `000024/000026` | reset vector while boot overlay is active |
| `000100..000106` | retained ODT/reset fallback words while overlay is active |
| `004000..004777` | SD bootstrap through MicROM spare bits |
| `000000..157776` | guest SPI FRAM, bank 0 |
| `160000..160476` | private RK611 interrupt service while selected |
| `177440..177476` | RK611 window; writable words use FRAM bank 1 |
| `177500` | SD SPI data register |
| `177502` | SD SPI control register |
| `177560..177566` | KL11 console |

Normal memory accesses use FRAM bank 0. RK611 register storage uses invisible
bank 1, so controller state does not consume FPGA registers.

## Reset and overlay removal

On reset AM4 reads vector `024`, gets PC `004000`, and starts the bootstrap
stored in MicROM spare bits. The overlay also retains a minimal loop at `100`
so ODT remains available if boot fails.

After loading sectors 0 and 1 into FRAM, the bootstrap writes control value 7
to `177502`. Bit 2 arms overlay removal. The following `CLR PC` is still
fetched from boot ROM; the first read at address zero removes the overlay and
exposes FRAM. Interrupts remain gated until this transition completes.

## UART and interrupts

`wbc_uart_xo2` is configured for 26.6 MHz and fixed 115200/8/N/1. It provides
KL11 registers at `177560..177566`; RX and TX request vectors `060` and `064`.
The board adapter chooses RX first when both are pending and acknowledges the
chosen request directly. The general `wbc_vic` is not instantiated.

The 50 Hz timer is an EVNT edge. It remains disarmed until RT-11 writes a
nonzero vector-100 handler to FRAM, preventing an early tick from returning
through address zero during the slow software RK service.

## RK611 execution model

The adapter recognizes GO with READ or WRITE in RKCS1 and requests a private
vector at `160000`. AM4 enters its ordinary interrupt path and executes
`rk_service.asm` from the spare-bit port. At fixed RTI word `160476` the
adapter releases the private overlay and, when interrupt enable was set,
schedules normal completion vector `210`.

Most controller policy stays in PDP-11 code. RTL stores command state and
arbitrates the vector; it does not implement a second disk DMA engine.
