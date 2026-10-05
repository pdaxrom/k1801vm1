; Read card capacity from CSD v1/v2 before accepting any MBR extent.
sd_capacity:
        clr r2
        clr r3
        mov #0377, r4
        mov #9., r0
        jsr pc, sd_command
        tst r0
        beq bios_far_6
        jmp sd_capacity_bad
bios_far_6:
        mov #65535., r5
sd_csd_token:
        mov #0377, r0
        jsr pc, spi_byte
        cmp r0, #0xfe
        beq sd_csd_read
        cmp r0, #0377
        bne sd_capacity_bad
        sob r5, sd_csd_token
        br sd_capacity_bad
sd_csd_read:
        mov #BUFFER, r1
        mov #16., r5
sd_csd_bytes:
        mov #0377, r0
        jsr pc, spi_byte
        movb r0, (r1)+
        sob r5, sd_csd_bytes
        mov #0377, r0
        jsr pc, spi_byte
        mov #0377, r0
        jsr pc, spi_byte
        mov #0x23, @#SPI_CTRL
        mov #0377, r0
        jsr pc, spi_byte
        movb @#BUFFER, r0
        bic #0177477, r0
        cmp r0, #0100
        beq sd_csd_v2
        tst r0
        bne sd_capacity_bad
        ; v1: (C_SIZE+1) << (C_SIZE_MULT+2+READ_BL_LEN-9).
        movb @#BUFFER+6, r0
        bic #177774, r0
        swab r0
        asl r0
        asl r0
        movb @#BUFFER+7, r1
        bic #177400, r1
        asl r1
        asl r1
        bis r1, r0
        movb @#BUFFER+8., r1
        bic #177477, r1
        mov #6, r2
sd_csd_size_low:
        asr r1
        sob r2, sd_csd_size_low
        bis r1, r0
        inc r0
        clr r1
        movb @#BUFFER+9., r2
        bic #177774, r2
        asl r2
        movb @#BUFFER+10., r3
        bic #177577, r3
        mov #7, r4
sd_csd_multiplier:
        asr r3
        sob r4, sd_csd_multiplier
        bis r3, r2
        movb @#BUFFER+5, r3
        bic #177760, r3
        cmp r3, #9.
        bcs sd_capacity_bad
        cmp r3, #11.
        bhi sd_capacity_bad
        add r3, r2
        sub #7, r2
        br sd_csd_shift
sd_csd_v2:
        movb @#BUFFER+8., r0
        swab r0
        bic #0377, r0
        movb @#BUFFER+9., r2
        bic #177400, r2
        bis r2, r0
        movb @#BUFFER+7, r1
        bic #177700, r1
        add #1, r0
        adc r1
        mov #10., r2
sd_csd_shift:
        asl r0
        rol r1
        bcs sd_capacity_bad
        sob r2, sd_csd_shift
        mov r0, @#CARD_LO
        mov r1, @#CARD_HI
        bis r1, r0
        beq sd_capacity_bad
        rts pc
sd_capacity_bad:
        mov #0x25, r0
        jmp fail

; Validate all four primary extents and their pairwise disjointness.
bios_check_mbr:
        mov #BIOS_MBR, r5
        clr r3
bios_mbr_extent:
        movb 4(r5), r0
        bic #177400, r0
        tst r0
        bne bios_mbr_present
        mov 8.(r5), r0
        bis 10.(r5), r0
        bis 12.(r5), r0
        bis 14.(r5), r0
        bne bios_mbr_bad
        clr r2
        clr r4
        br bios_mbr_end
bios_mbr_present:
        cmp r0, #5
        beq bios_mbr_extended
        cmp r0, #017
        beq bios_mbr_extended
        cmp r0, #0205
        beq bios_mbr_extended
        cmp r0, #0356
        beq bios_mbr_bad
        br bios_mbr_bounds
bios_mbr_extended:
        cmp r3, #3
        bne bios_mbr_bad
bios_mbr_bounds:
        mov 8.(r5), r0
        bis 10.(r5), r0
        beq bios_mbr_bad
        mov 12.(r5), r2
        mov 14.(r5), r4
        mov r2, r0
        bis r4, r0
        beq bios_mbr_bad
        add 8.(r5), r2
        adc r4
        bcs bios_mbr_bad
        add 10.(r5), r4
        bcs bios_mbr_bad
        cmp r4, @#CARD_HI
        bhi bios_mbr_bad
        bcs bios_mbr_end
        cmp r2, @#CARD_LO
        bhi bios_mbr_bad
bios_mbr_end:
        mov r3, r0
        asl r0
        asl r0
        add #BIOS_ENDS, r0
        mov r2, (r0)+
        mov r4, (r0)
        inc r3
        add #16., r5
        cmp r3, #4
        bcs bios_mbr_extent
        ; For every pair, one start must be at or beyond the other's end.
        mov #BIOS_MBR, r5
        mov #BIOS_ENDS, r4
        mov #3, r3
bios_mbr_outer:
        tstb 4(r5)
        beq bios_mbr_outer_next
        mov r5, r2
        add #16., r2
        mov r4, r1
        add #4, r1
        mov r3, r0
bios_mbr_inner:
        tstb 4(r2)
        beq bios_mbr_inner_next
        cmp 10.(r5), 2(r1)
        bhi bios_mbr_inner_next
        bcs bios_mbr_check_reverse
        cmp 8.(r5), (r1)
        bcc bios_mbr_inner_next
bios_mbr_check_reverse:
        cmp 10.(r2), 2(r4)
        bhi bios_mbr_inner_next
        bcs bios_mbr_bad
        cmp 8.(r2), (r4)
        bcs bios_mbr_bad
bios_mbr_inner_next:
        add #16., r2
        add #4, r1
        sob r0, bios_mbr_inner
bios_mbr_outer_next:
        add #16., r5
        add #4, r4
        sob r3, bios_mbr_outer
        rts pc
bios_mbr_bad:
        jmp bad_mbr
