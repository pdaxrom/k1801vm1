        org     002000

DL11_RCSR equ 0177560
DL11_RBUF equ 0177562
DL11_TCSR equ 0177564
DL11_TBUF equ 0177566

CSR_IE    equ 000100

start:
        mtps    #000000
        mov     #stack_end, sp
        clrb    rx_seen

        mov     #rx_isr, r0
        mov     r0, @#000060
        mov     #000340, r0
        mov     r0, @#000062
        mov     #tx_isr, r0
        mov     r0, @#000064
        mov     #000340, r0
        mov     r0, @#000066

        mov     #msg, r1

        movb    #CSR_IE, @#DL11_TCSR
        movb    #101, @#DL11_TBUF

wait_tx_irq:
        tstb    tx_irq_seen
        beq     wait_tx_irq

tx_poll_loop:
        movb    @#DL11_TCSR, r0
        bitb    #0200, r0
        beq     tx_poll_loop
        movb    (r1)+, r0
        beq     tx_poll_done
        movb    r0, @#DL11_TBUF
        br      tx_poll_loop
tx_poll_done:

        movb    #CSR_IE, @#DL11_RCSR

wait_rx:
        tstb    rx_seen
        beq     wait_rx

        halt

tx_isr:
        movb    #1, tx_irq_seen
        clrb    @#DL11_TCSR
        rti

rx_isr:
        mov     r0, -(sp)
        movb    @#DL11_RBUF, r0
        movb    r0, last_rx
        movb    #1, rx_seen
        mov     (sp)+, r0
        rti

        even
tx_irq_seen:
        DB      000
rx_seen:
        DB      000
last_rx:
        DB      000

msg:
        DB      "DL11 IRQ OK\r\n", 0

        even
stack:
        DS      128
stack_end:
