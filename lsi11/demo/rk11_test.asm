        org     002000

DL11_TCSR equ 0177564
DL11_TBUF equ 0177566

RKDS      equ 0177400
RKER      equ 0177402
RKCS      equ 0177404
RKWC      equ 0177406
RKBA      equ 0177410
RKDA      equ 0177412

start:
        mtps    #000000
        mov     #stack_end, sp

        mov     #rk_isr, r0
        mov     r0, @#000220
        mov     #000340, r0
        mov     r0, @#000222

        mov     #msg_banner, r1
        jsr     pc, print_str

        clrb    rk_flag
        mov     #rk_buf, r0
        mov     r0, @#RKBA
        mov     #177777, r0
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
        cmp     r0, #rk_buf+2
        bne     rk_fail
        mov     #msg_ok, r1
        jsr     pc, print_str
        br      rk_done
rk_fail:
        mov     #msg_fail, r1
        jsr     pc, print_str
        br      rk_done
rk_skip:
        mov     #msg_skip, r1
        jsr     pc, print_str
rk_done:
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

rk_isr:
        mov     r0, -(sp)
        movb    #1, rk_flag
        mov     #000000, r0
        mov     r0, @#RKCS
        mov     (sp)+, r0
        rti

        even
rk_flag:
        DB      000

        even
rk_buf:
        DS      32

msg_banner:
        DB      "RK11 TEST\r\n", 0
msg_ok:
        DB      "RK11 DMA OK\r\n", 0
msg_skip:
        DB      "RK11 DMA SKIP\r\n", 0
msg_fail:
        DB      "RK11 DMA FAIL\r\n", 0

        even
stack:
        DS      128
stack_end:
