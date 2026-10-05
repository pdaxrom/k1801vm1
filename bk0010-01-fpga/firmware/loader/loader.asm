; Second-stage native VM1 loader. Resident services stay visible until commit.
OPEN equ 100004
GETBYTE equ 100010
FAIL equ 100014
HANDOFF equ 100020
SHOWSTAGE equ 100024
ADDRESS equ 177614
ROM_WRITE equ 177616
ROM_READ equ 177620
CRC_FEED equ 177622
CRC_RESET equ 177624
CRC_LOW equ 177626
CRC_HIGH equ 177630
EXPECT_LOW equ 177632
EXPECT_HIGH equ 177634
HEADER equ 03000
HEADER_CRC_LOW equ 01100
HEADER_CRC_HIGH equ 01102

.org 020000
        mov #4, r0
        jsr pc, @#SHOWSTAGE
        mov #rom_name, r0
        jsr pc, @#OPEN
        cmp r0, #33280.
        beq size_ok
        jmp bad_header
size_ok:
        mov #HEADER, r1
        mov #512., r2
header_read:
        jsr pc, @#GETBYTE
        movb r0, (r1)+
        sob r2, header_read
        cmp @#HEADER, #0x4b42
        bne bad_header
        cmp @#HEADER+2, #0x3030
        bne bad_header
        cmp @#HEADER+4, #0x3031
        bne bad_header
        tst @#HEADER+6
        bne bad_header
        cmp @#HEADER+8., #1
        bne bad_header
        cmp @#HEADER+10., #512.
        bne bad_header
        cmp @#HEADER+12., #0x8000
        bne bad_header
        tst @#HEADER+14.
        bne bad_header
        cmp @#HEADER+16., #0x8000
        bne bad_header
        tst @#HEADER+18.
        bne bad_header
        cmp @#HEADER+20., #0x8000
        bne bad_header
        tst @#HEADER+22.
        bne bad_header
        mov @#HEADER+28., @#HEADER_CRC_LOW
        mov @#HEADER+30., @#HEADER_CRC_HIGH
        clr @#HEADER+28.
        clr @#HEADER+30.
        clr @#CRC_RESET
        mov #HEADER, r1
        mov #512., r2
header_crc:
        movb (r1)+, @#CRC_FEED
        sob r2, header_crc
        cmp @#CRC_LOW, @#HEADER_CRC_LOW
        bne bad_header
        cmp @#CRC_HIGH, @#HEADER_CRC_HIGH
        bne bad_header
        br header_valid
bad_header:
        mov #0x41, r0
        jmp @#FAIL
header_valid:
        mov @#HEADER+24., @#EXPECT_LOW
        mov @#HEADER+26., @#EXPECT_HIGH
        mov #5, r0
        jsr pc, @#SHOWSTAGE
        clr @#CRC_RESET
        mov #0x8000, @#ADDRESS
        mov #32768., r2
payload_read:
        jsr pc, @#GETBYTE
        movb r0, @#ROM_WRITE
        sob r2, payload_read
        cmp @#CRC_LOW, @#EXPECT_LOW
        bne bad_payload
        cmp @#CRC_HIGH, @#EXPECT_HIGH
        bne bad_payload
        mov #6, r0
        jsr pc, @#SHOWSTAGE
        mov #0x8000, @#ADDRESS
        clr @#CRC_RESET
        mov #32768., r2
sram_readback:
        movb @#ROM_READ, r0
        sob r2, sram_readback
        cmp @#CRC_LOW, @#EXPECT_LOW
        bne bad_sram
        cmp @#CRC_HIGH, @#EXPECT_HIGH
        bne bad_sram
        mov #7, r0
        jsr pc, @#SHOWSTAGE
        jmp @#HANDOFF
bad_payload:
        mov #0x42, r0
        jmp @#FAIL
bad_sram:
        mov #0x43, r0
        jmp @#FAIL
rom_name:
        db "BK0010  ROM"
        even
