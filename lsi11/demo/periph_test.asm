        org     002000

DL11_RCSR equ 0177560
DL11_RBUF equ 0177562
DL11_TCSR equ 0177564
DL11_TBUF equ 0177566

KW11_CSR  equ 0177546

LP11_CSR  equ 0177514
LP11_DBR  equ 0177516

RKDS      equ 0177400
RKER      equ 0177402
RKCS      equ 0177404
RKWC      equ 0177406
RKBA      equ 0177410
RKDA      equ 0177412

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
        mov     #kw_isr, r0
        mov     r0, @#000100
        mov     #000340, r0
        mov     r0, @#000102
        mov     #lp_isr, r0
        mov     r0, @#000200
        mov     #000340, r0
        mov     r0, @#000202
        mov     #rk_isr, r0
        mov     r0, @#000220
        mov     #000340, r0
        mov     r0, @#000222

        movb    #0, @#DL11_TCSR

        mov     #msg_banner, r1
        jsr     pc, print_str

        mov     #msg_dl11_tx, r1
        mov     r1, tx_ptr
        clrb    tx_done
        movb    #CSR_IE, @#DL11_TCSR
        movb    (r1)+, r0
        mov     r1, tx_ptr
        movb    r0, @#DL11_TBUF
wait_tx:
        tstb    tx_done
        beq     wait_tx
        mov     #msg_dl11_tx_ok, r1
        jsr     pc, print_str

        mov     #msg_dl11_rx_prompt, r1
        jsr     pc, print_str
        clrb    rx_flag
        movb    #CSR_IE, @#DL11_RCSR
wait_rx:
        tstb    rx_flag
        beq     wait_rx
        movb    #0, @#DL11_RCSR
        mov     #msg_dl11_rx_ok, r1
        jsr     pc, print_str

        clrb    kw_flag
        movb    #CSR_IE, @#KW11_CSR
wait_kw:
        tstb    kw_flag
        beq     wait_kw
        movb    #0, @#KW11_CSR
        mov     #msg_kw11_ok, r1
        jsr     pc, print_str

        mov     #msg_lp11, r1
        mov     r1, lp_ptr
        clrb    lp_done
        movb    #CSR_IE, @#LP11_CSR
        movb    (r1)+, r0
        mov     r1, lp_ptr
        movb    r0, @#LP11_DBR
wait_lp:
        tstb    lp_done
        beq     wait_lp
        mov     #msg_lp11_ok, r1
        jsr     pc, print_str

        clrb    rk_flag
        mov     #rk_buf, r0
        mov     r0, @#RKBA
        mov     #177760, r0
        mov     r0, @#RKWC
        mov     #000001, r0
        mov     r0, @#RKDA
        mov     #000105, r0
        mov     r0, @#RKCS
wait_rk:
        tstb    rk_flag
        beq     wait_rk
        mov     @#RKER, r0
        bne     rk_skip
        mov     @#RKWC, r0
        bne     rk_fail
        mov     @#RKBA, r0
        cmp     r0, #rk_buf+040
        bne     rk_fail
        mov     #msg_rk11_ok, r1
        jsr     pc, print_str
        br      rk_done
rk_fail:
        mov     #msg_rk11_fail, r1
        jsr     pc, print_str
        br      rk_done
rk_skip:
        mov     #msg_rk11_skip, r1
        jsr     pc, print_str
rk_done:

        mov     #msg_done, r1
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

kw_isr:
        mov     r0, -(sp)
        movb    #1, kw_flag
        movb    #0, @#KW11_CSR
        mov     (sp)+, r0
        rti

lp_isr:
        mov     r0, -(sp)
        mov     r1, -(sp)

        mov     lp_ptr, r1
        movb    (r1)+, r0
        beq     lp_done_isr
        mov     r1, lp_ptr
        movb    r0, @#LP11_DBR

        mov     (sp)+, r1
        mov     (sp)+, r0
        rti

lp_done_isr:
        mov     r1, lp_ptr
        movb    #1, lp_done
        movb    #0, @#LP11_CSR

        mov     (sp)+, r1
        mov     (sp)+, r0
        rti

rk_isr:
        mov     r0, -(sp)
        movb    #1, rk_flag
        mov     #000000, r0
        mov     r0, @#RKCS
        mov     (sp)+, r0
        rti

        even
tx_ptr:
        DW      000000
tx_done:
        DB      000
rx_flag:
        DB      000
kw_flag:
        DB      000
lp_ptr:
        DW      000000
lp_done:
        DB      000
rk_flag:
        DB      000
last_rx:
        DB      000

        even
rk_buf:
        DS      32

msg_banner:
        DB      "LSI11 PERIPH TEST\r\n", 0
msg_dl11_tx:
        DB      "DL11 TX IRQ\r\n", 0
msg_dl11_tx_ok:
        DB      "DL11 TX IRQ OK\r\n", 0
msg_dl11_rx_prompt:
        DB      "PRESS KEY FOR DL11 RX...\r\n", 0
msg_dl11_rx_ok:
        DB      "DL11 RX IRQ OK\r\n", 0
msg_kw11_ok:
        DB      "KW11 IRQ OK\r\n", 0
msg_lp11:
        DB      "LP11 IRQ\r\n", 0
msg_lp11_ok:
        DB      "LP11 IRQ OK\r\n", 0
msg_rk11_ok:
        DB      "RK11 DMA OK\r\n", 0
msg_rk11_skip:
        DB      "RK11 DMA SKIP\r\n", 0
msg_rk11_fail:
        DB      "RK11 DMA FAIL\r\n", 0
msg_done:
        DB      "DONE\r\n", 0

        even
stack:
        DS      128
stack_end:
