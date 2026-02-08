        org     002000

DL11_TCSR equ 0177564
DL11_TBUF equ 0177566
LP11_CSR  equ 0177514
LP11_DBR  equ 0177516

CSR_IE    equ 000100

start:
        mtps    #000000
        mov     #stack_end, sp

        mov     #lp_isr, r0
        mov     r0, @#000200
        mov     #000340, r0
        mov     r0, @#000202

        mov     #msg_banner, r1
        jsr     pc, print_str

        mov     #msg_lp, r1
        mov     r1, lp_ptr
        clrb    lp_done
        movb    #CSR_IE, @#LP11_CSR
        movb    (r1)+, r0
        mov     r1, lp_ptr
        movb    r0, @#LP11_DBR
wait_lp:
        tstb    lp_done
        beq     wait_lp
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

        even
lp_ptr:
        DW      000000
lp_done:
        DB      000

msg_banner:
        DB      "LP11 TEST\r\n", 0
msg_lp:
        DB      "LP11 IRQ\r\n", 0
msg_ok:
        DB      "LP11 IRQ OK\r\n", 0

        even
stack:
        DS      128
stack_end:
