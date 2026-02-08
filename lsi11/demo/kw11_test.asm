        org     002000

DL11_TCSR equ 0177564
DL11_TBUF equ 0177566
KW11_CSR  equ 0177546

CSR_IE    equ 000100

start:
        mtps    #000000
        mov     #stack_end, sp

        mov     #kw_isr, r0
        mov     r0, @#000100
        mov     #000340, r0
        mov     r0, @#000102

        mov     #msg_banner, r1
        jsr     pc, print_str

        clrb    kw_flag
        movb    #CSR_IE, @#KW11_CSR
wait_kw:
        tstb    kw_flag
        beq     wait_kw
        movb    #0, @#KW11_CSR

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

kw_isr:
        mov     r0, -(sp)
        movb    #1, kw_flag
        movb    #0, @#KW11_CSR
        mov     (sp)+, r0
        rti

        even
kw_flag:
        DB      000

msg_banner:
        DB      "KW11 TEST\r\n", 0
msg_ok:
        DB      "KW11 OK\r\n", 0

        even
stack:
        DS      128
stack_end:
