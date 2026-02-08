        org     002000

DL11_TCSR equ 0177564
DL11_TBUF equ 0177566

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

        mov     #write_buf, r0
        mov     r0, @#RKBA
        mov     #177770, r0
        mov     r0, @#RKWC
        mov     #000001, r0
        mov     r0, @#RKDA
        mov     #000103, r0
        mov     r0, @#RKCS
wait_wr:
        tstb    rk_flag
        beq     wait_wr
        clrb    rk_flag

        mov     @#RKER, r0
        bne     rk_skip

        mov     #read_buf, r1
        mov     #010, r2
clr_loop:
        clr     (r1)+
        sob     r2, clr_loop

        mov     #read_buf, r0
        mov     r0, @#RKBA
        mov     #177770, r0
        mov     r0, @#RKWC
        mov     #000001, r0
        mov     r0, @#RKDA
        mov     #000105, r0
        mov     r0, @#RKCS
wait_rd:
        tstb    rk_flag
        beq     wait_rd

        mov     @#RKER, r0
        bne     rk_skip

        mov     #write_buf, r0
        mov     #read_buf, r1
        mov     #010, r2
cmp_loop:
        cmp     (r0)+, (r1)+
        bne     rk_fail
        sob     r2, cmp_loop

        mov     #msg_ok, r1
        jsr     pc, print_str
        halt

rk_fail:
        mov     #msg_fail, r1
        jsr     pc, print_str
        halt

rk_skip:
        mov     #msg_skip, r1
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
write_buf:
        DW      012345
        DW      065432
        DW      000001
        DW      177777
        DW      000123
        DW      045670
        DW      012340
        DW      076543

read_buf:
        DSW     010

msg_banner:
        DB      "RK11 WR TEST\r\n", 0
msg_ok:
        DB      "RK11 WR OK\r\n", 0
msg_fail:
        DB      "RK11 WR FAIL\r\n", 0
msg_skip:
        DB      "RK11 WR SKIP\r\n", 0

        even
stack:
        DS      128
stack_end:
