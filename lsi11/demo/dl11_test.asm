        org     002000

DL11_RCSR equ 0177560
DL11_RBUF equ 0177562
DL11_TCSR equ 0177564
DL11_TBUF equ 0177566

CSR_IE    equ 000100

start:
        mtps    #000000
        mov     #stack_end, sp

        mov     #rx_isr, r0
        mov     r0, @#000060
        mov     #000340, r0
        mov     r0, @#000062
        mov     #tx_isr, r0
        mov     r0, @#000064
        mov     #000340, r0
        mov     r0, @#000066

        mov     #msg_banner, r1
        jsr     pc, print_str

        mov     #msg_tx, r1
        mov     r1, tx_ptr
        clrb    tx_done
        movb    #CSR_IE, @#DL11_TCSR
wait_tx:
        tstb    tx_done
        beq     wait_tx
        mov     #msg_tx_ok, r1
        jsr     pc, print_str

        clrb    rx_flag
        mov     #msg_prompt, r1
        jsr     pc, print_str
        movb    #CSR_IE, @#DL11_RCSR
wait_rx:
        tstb    rx_flag
        beq     wait_rx
        movb    #0, @#DL11_RCSR

        mov     #msg_ok, r1
        jsr     pc, print_str
        halt

putc:
putc_wait:
        movb    @#DL11_TCSR, r2
        bitb    #0200, r2
        beq     putc_wait
        movb    r0, @#DL11_TBUF
        rts     pc

print_str:
        movb    (r1)+, r0
        beq     print_done
        jsr     pc, putc
        br      print_str
print_done:
        rts     pc

tx_isr:
        mov     r0, -(sp)
        mov     r1, -(sp)

        mov     tx_ptr, r1
        movb    (r1)+, r0
        beq     tx_done_isr
        mov     r1, tx_ptr
        movb    r0, @#DL11_TBUF

        mov     (sp)+, r1
        mov     (sp)+, r0
        rti

tx_done_isr:
        mov     r1, tx_ptr
        movb    #1, tx_done
        movb    #0, @#DL11_TCSR

        mov     (sp)+, r1
        mov     (sp)+, r0
        rti

rx_isr:
        mov     r0, -(sp)
        movb    @#DL11_RBUF, r0
        movb    r0, last_rx
        movb    #1, rx_flag
        mov     (sp)+, r0
        rti

        even
tx_ptr:
        DW      000000
tx_done:
        DB      000
rx_flag:
        DB      000
last_rx:
        DB      000

msg_banner:
        DB      "DL11 TEST\r\n", 0
msg_prompt:
        DB      "PRESS KEY...\r\n", 0
msg_tx_ok:
        DB      "DL11 TX IRQ OK\r\n", 0
msg_tx:
        DB      "DL11 TX IRQ\r\n", 0
msg_ok:
        DB      "DL11 RX IRQ OK\r\n", 0

        even
stack:
        DS      128
stack_end:
