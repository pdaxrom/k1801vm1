; Native VM1 second stage. Validated bundles contain physical ROM segments.
OPEN equ 100004
GETBYTE equ 100010
FAIL equ 100014
HANDOFF equ 100020
SHOWSTAGE equ 100024
TRYOPEN equ 100030
CONTRACT equ 100034
ADDRESS equ 177614
ROM_WRITE equ 177616
ROM_READ equ 177620
CRC_FEED equ 177622
CRC_RESET equ 177624
CRC_LOW equ 177626
CRC_HIGH equ 177630
EXPECT_LOW equ 177632
EXPECT_HIGH equ 177634
ADDRESS_HIGH equ 177640
PAYLOAD_SIZE equ 177642
HEADER equ 03000
HEADER_CRC_LOW equ 01100
HEADER_CRC_HIGH equ 01102
FILE_SIZE equ 01104
SEGMENTS equ 01106
BIOS_MODEL_ID equ 01411

.org 020000
        mov #4, r0
        jsr pc, @#SHOWSTAGE
        movb @#BIOS_MODEL_ID, r0
        add #060, r0
        movb r0, @#rom_name+3
        mov #rom_name, r0
        jsr pc, @#TRYOPEN
        tst r0
        bne loader_file_open
        ; Existing BK0010.ROM cards remain usable in model 2.
        cmpb @#BIOS_MODEL_ID, #2
        beq loader_far_0
        jmp missing_rom
loader_far_0:
        mov #legacy_name, r0
        jsr pc, @#OPEN
loader_file_open:
        mov r0, @#FILE_SIZE
        cmp r0, #512.
        bhi loader_far_1
        jmp bad_header
loader_far_1:
        mov #HEADER, r1
        mov #512., r2
header_read:
        jsr pc, @#GETBYTE
        movb r0, (r1)+
        sob r2, header_read
        cmp @#HEADER, #0x4b42
        beq loader_far_2
        jmp bad_header
loader_far_2:
        tst @#HEADER+6
        beq loader_far_3
        jmp bad_header
loader_far_3:
        cmp @#HEADER+10., #512.
        beq loader_far_4
        jmp bad_header
loader_far_4:
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
        beq loader_far_5
        jmp bad_header
loader_far_5:
        cmp @#CRC_HIGH, @#HEADER_CRC_HIGH
        beq loader_far_6
        jmp bad_header
loader_far_6:
        cmp @#HEADER+8., #2
        bne legacy_header
        cmp @#HEADER+2, #0x4f52
        beq loader_far_7
        jmp bad_header
loader_far_7:
        cmp @#HEADER+4, #0x324d
        beq loader_far_8
        jmp bad_header
loader_far_8:
        clr r0
        movb @#BIOS_MODEL_ID, r0
        cmp @#HEADER+12., r0
        beq loader_far_9
        jmp bad_header
loader_far_9:
        tst @#HEADER+18.
        beq loader_far_10
        jmp bad_header
loader_far_10:
        tst @#HEADER+22.
        beq loader_far_11
        jmp bad_header
loader_far_11:
        mov @#FILE_SIZE, r0
        sub #512., r0
        cmp @#HEADER+20., r0
        beq loader_far_12
        jmp bad_header
loader_far_12:
        mov #HEADER, r1
        jsr pc, @#CONTRACT
        tst r0
        bne loader_far_13
        jmp bad_header
loader_far_13:
        mov @#HEADER+14., @#SEGMENTS
        mov @#SEGMENTS, r2
        asl r2
        asl r2
        asl r2
        asl r2
        add #HEADER+32., r2
        mov #HEADER+512., r1
        sub r2, r1
        asr r1
header_reserved:
        tst (r2)+
        beq loader_far_14
        jmp bad_header
loader_far_14:
        sob r1, header_reserved
        br header_valid
legacy_header:
        cmpb @#BIOS_MODEL_ID, #2
        beq loader_far_15
        jmp bad_header
loader_far_15:
        cmp @#HEADER+2, #0x3030
        beq loader_far_16
        jmp bad_header
loader_far_16:
        cmp @#HEADER+4, #0x3031
        beq loader_far_17
        jmp bad_header
loader_far_17:
        cmp @#HEADER+8., #1
        bne bad_header
        cmp @#FILE_SIZE, #33280.
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
        mov #1, @#SEGMENTS
        ; Convert the legacy descriptor to the physical segment format.
        mov #0, @#HEADER+32.
        mov #2, @#HEADER+34.
        mov #32768., @#HEADER+36.
        mov @#HEADER+24., @#HEADER+44.
        mov @#HEADER+26., @#HEADER+46.
header_valid:
        mov @#HEADER+20., @#PAYLOAD_SIZE
        mov @#HEADER+24., @#EXPECT_LOW
        mov @#HEADER+26., @#EXPECT_HIGH
        mov #5, r0
        jsr pc, @#SHOWSTAGE
        mov #HEADER+32., r3
        mov @#SEGMENTS, r4
payload_segment:
        mov (r3), @#ADDRESS
        mov 2(r3), @#ADDRESS_HIGH
        mov 4(r3), r2
        clr @#CRC_RESET
payload_read:
        jsr pc, @#GETBYTE
        movb r0, @#ROM_WRITE
        sob r2, payload_read
        cmp @#CRC_LOW, 12.(r3)
        bne bad_payload
        cmp @#CRC_HIGH, 14.(r3)
        bne bad_payload
        add #16., r3
        sob r4, payload_segment
        mov #6, r0
        jsr pc, @#SHOWSTAGE
        clr @#CRC_RESET
        mov #HEADER+32., r3
        mov @#SEGMENTS, r4
sram_segment:
        mov (r3), @#ADDRESS
        mov 2(r3), @#ADDRESS_HIGH
        mov 4(r3), r2
sram_readback:
        movb @#ROM_READ, r0
        sob r2, sram_readback
        add #16., r3
        sob r4, sram_segment
        cmp @#CRC_LOW, @#EXPECT_LOW
        bne bad_sram
        cmp @#CRC_HIGH, @#EXPECT_HIGH
        bne bad_sram
        mov #7, r0
        jsr pc, @#SHOWSTAGE
        jmp @#HANDOFF
missing_rom:
        mov #0x44, r0
        jmp @#FAIL
bad_header:
        mov #0x41, r0
        jmp @#FAIL
bad_payload:
        mov #0x42, r0
        jmp @#FAIL
bad_sram:
        mov #0x43, r0
        jmp @#FAIL
rom_name:
        db "BK02    ROM"
legacy_name:
        db "BK0010  ROM"
        even
