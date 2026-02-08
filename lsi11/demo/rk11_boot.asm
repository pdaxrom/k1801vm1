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
        mov     #001000, sp

        mov     #msg_banner, r1
        jsr     pc, print_str

        mov     #000000, r0
        mov     r0, @#RKBA
        mov     #177400, r0
        mov     r0, @#RKWC
        mov     #000000, r0
        mov     r0, @#RKDA
        mov     #000005, r0
        mov     r0, @#RKCS
wait0:
        movb    @#RKCS, r0
        bitb    #0200, r0
        beq     wait0
        mov     @#RKER, r0
        beq     boot

        mov     #000001, r0
        mov     r0, @#RKDA
        mov     #000005, r0
        mov     r0, @#RKCS
wait1:
        movb    @#RKCS, r0
        bitb    #0200, r0
        beq     wait1
        mov     @#RKER, r0
        beq     boot

        mov     #msg_fail, r1
        jsr     pc, print_str
        halt

boot:
        mov     #000000, pc

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

msg_banner:
        DB      "RK11 BOOT\r\n", 0
msg_fail:
        DB      "RK11 BOOT FAIL\r\n", 0
