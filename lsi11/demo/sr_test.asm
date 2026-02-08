        org     002000

DL11_TCSR equ 0177564
DL11_TBUF equ 0177566
SR_ADDR   equ 0177570

start:
        mtps    #000000
        mov     #stack_end, sp

        mov     #msg_banner, r1
        jsr     pc, print_str

        mov     @#SR_ADDR, r0
        beq     sr_ok
        mov     #msg_fail, r1
        jsr     pc, print_str
        halt
sr_ok:
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

msg_banner:
        DB      "SR TEST\r\n", 0
msg_ok:
        DB      "SR OK\r\n", 0
msg_fail:
        DB      "SR FAIL\r\n", 0

        even
stack:
        DS      128
stack_end:
