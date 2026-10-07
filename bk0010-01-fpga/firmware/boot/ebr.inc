; The first two logical disks in primary partition 4 belong to BK.
; Validate their bounds and links; leave any later disks to other machines.
bios_read_logical:
        mov #BIOS_LOGICAL, r1
        mov #16., r2
bios_logical_clear:
        clr (r1)+
        sob r2, bios_logical_clear
        cmpb @#BIOS_MBR+52., #5
        beq bios_logical_start
        cmpb @#BIOS_MBR+52., #017
        beq bios_logical_start
        cmpb @#BIOS_MBR+52., #0205
        beq bios_logical_start
        rts pc
bios_logical_start:
        mov @#BIOS_MBR+56., @#EBR_BASE
        mov @#BIOS_MBR+58., @#EBR_BASE+2
        mov @#BIOS_ENDS+12., @#EBR_END
        mov @#BIOS_ENDS+14., @#EBR_END+2
        mov @#EBR_BASE, @#EBR_LBA
        mov @#EBR_BASE+2, @#EBR_LBA+2
        mov #BIOS_LOGICAL, r5
bios_logical_read:
        mov @#EBR_LBA, @#SECTOR_LO
        mov @#EBR_LBA+2, @#SECTOR_HI
        jsr pc, read_sector
        cmp @#BUFFER+510., #0xaa55
        beq bios_ebr_guard_0
        jmp bios_ebr_bad
bios_ebr_guard_0:
        mov #BUFFER+478., r1
        mov #16., r2
        clr r0
bios_ebr_extra:
        bis (r1)+, r0
        sob r2, bios_ebr_extra
        tst r0
        beq bios_ebr_guard_1
        jmp bios_ebr_bad
bios_ebr_guard_1:
        bitb #0177, @#BUFFER+446.
        beq bios_ebr_guard_2
        jmp bios_ebr_bad
bios_ebr_guard_2:
        tstb @#BUFFER+450.
        bne bios_ebr_guard_3
        jmp bios_ebr_bad
bios_ebr_guard_3:
        cmpb @#BUFFER+450., #5
        bne bios_ebr_guard_4
        jmp bios_ebr_bad
bios_ebr_guard_4:
        cmpb @#BUFFER+450., #017
        bne bios_ebr_guard_5
        jmp bios_ebr_bad
bios_ebr_guard_5:
        cmpb @#BUFFER+450., #0205
        bne bios_ebr_guard_6
        jmp bios_ebr_bad
bios_ebr_guard_6:
        cmpb @#BUFFER+450., #0356
        bne bios_ebr_guard_7
        jmp bios_ebr_bad
bios_ebr_guard_7:
        mov #BUFFER+446., r1
        mov r5, r2
        mov #8., r4
bios_ebr_copy:
        mov (r1)+, (r2)+
        sob r4, bios_ebr_copy
        mov 8.(r5), r0
        bis 10.(r5), r0
        bne bios_ebr_guard_8
        jmp bios_ebr_bad
bios_ebr_guard_8:
        add @#EBR_LBA, 8.(r5)
        adc 10.(r5)
        bcc bios_ebr_guard_9
        jmp bios_ebr_bad
bios_ebr_guard_9:
        add @#EBR_LBA+2, 10.(r5)
        bcc bios_ebr_guard_10
        jmp bios_ebr_bad
bios_ebr_guard_10:
        mov 12.(r5), r2
        mov 14.(r5), r4
        mov r2, r0
        bis r4, r0
        bne bios_ebr_guard_11
        jmp bios_ebr_bad
bios_ebr_guard_11:
        add 8.(r5), r2
        adc r4
        bcc bios_ebr_guard_12
        jmp bios_ebr_bad
bios_ebr_guard_12:
        add 10.(r5), r4
        bcc bios_ebr_guard_13
        jmp bios_ebr_bad
bios_ebr_guard_13:
        jsr pc, bios_ebr_bound
        mov r2, @#EBR_DATA_END
        mov r4, @#EBR_DATA_END+2
        tstb @#BUFFER+462.
        beq bios_ebr_guard_14
        jmp bios_ebr_bad
bios_ebr_guard_14:
        tstb @#BUFFER+466.
        bne bios_ebr_link
        mov @#BUFFER+470., r0
        bis @#BUFFER+472., r0
        bis @#BUFFER+474., r0
        bis @#BUFFER+476., r0
        beq bios_ebr_guard_15
        jmp bios_ebr_bad
bios_ebr_guard_15:
        rts pc
bios_ebr_link:
        cmpb @#BUFFER+466., #5
        beq bios_ebr_next
        cmpb @#BUFFER+466., #017
        beq bios_ebr_next
        cmpb @#BUFFER+466., #0205
        beq bios_ebr_guard_16
        jmp bios_ebr_bad
bios_ebr_guard_16:
bios_ebr_next:
        mov @#BUFFER+470., r2
        mov @#BUFFER+472., r4
        mov r2, r0
        bis r4, r0
        bne bios_ebr_guard_17
        jmp bios_ebr_bad
bios_ebr_guard_17:
        add @#EBR_BASE, r2
        adc r4
        bcc bios_ebr_guard_18
        jmp bios_ebr_bad
bios_ebr_guard_18:
        add @#EBR_BASE+2, r4
        bcc bios_ebr_guard_19
        jmp bios_ebr_bad
bios_ebr_guard_19:
        cmp r4, @#EBR_DATA_END+2
        bcc bios_ebr_guard_20
        jmp bios_ebr_bad
bios_ebr_guard_20:
        bhi bios_ebr_after_data
        cmp r2, @#EBR_DATA_END
        bcc bios_ebr_guard_21
        jmp bios_ebr_bad
bios_ebr_guard_21:
bios_ebr_after_data:
        mov r2, @#EBR_LBA
        mov r4, @#EBR_LBA+2
        mov @#BUFFER+474., r0
        bis @#BUFFER+476., r0
        bne bios_ebr_guard_22
        jmp bios_ebr_bad
bios_ebr_guard_22:
        add @#BUFFER+474., r2
        adc r4
        bcc bios_ebr_guard_23
        jmp bios_ebr_bad
bios_ebr_guard_23:
        add @#BUFFER+476., r4
        bcc bios_ebr_guard_24
        jmp bios_ebr_bad
bios_ebr_guard_24:
        jsr pc, bios_ebr_bound
        add #16., r5
        cmp r5, #BIOS_LOGICAL+32.
        bcs bios_ebr_guard_25
        rts pc
bios_ebr_guard_25:
        jmp bios_logical_read
bios_ebr_bound:
        cmp r4, @#EBR_END+2
        blos bios_ebr_guard_26
        jmp bios_ebr_bad
bios_ebr_guard_26:
        bcs bios_ebr_bound_ok
        cmp r2, @#EBR_END
        blos bios_ebr_guard_27
        jmp bios_ebr_bad
bios_ebr_guard_27:
bios_ebr_bound_ok:
        rts pc
bios_ebr_bad:
        jmp bad_mbr

; R0=partition 1..6 -> pointer to an absolute MBR-format descriptor.
; Logical descriptors live beyond both sector buffers and the menu stack.
bios_partition_address:
        dec r0
        cmp r0, #4
        bcs bios_partition_primary
        sub #4, r0
        asl r0
        asl r0
        asl r0
        asl r0
        add #BIOS_LOGICAL, r0
        rts pc
bios_partition_primary:
        asl r0
        asl r0
        asl r0
        asl r0
        add #BIOS_MBR, r0
        rts pc
