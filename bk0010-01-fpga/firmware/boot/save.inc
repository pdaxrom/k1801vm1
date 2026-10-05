
bios_save:
        jsr pc, bios_cfg_crc
        jsr pc, bios_validate
        tst r0
        beq bios_save_invalid
        tst @#BIOS_SD_READY
        beq bios_save_unavailable
        tst @#BIOS_FOUND
        beq bios_allocate
        bit #1, @#BIOS_CFG_ATTR
        bne bios_save_readonly
        cmp @#BIOS_CFG_SIZE, #16.
        bne bios_save_metadata
        mov @#BIOS_CFG_CLUSTER, r0
        br bios_save_data
bios_save_invalid:
        mov #0x51, @#BIOS_ERROR
        br bios_save_failed
bios_save_unavailable:
        mov #0x50, @#BIOS_ERROR
        br bios_save_failed
bios_save_readonly:
        mov #0x53, @#BIOS_ERROR
        br bios_save_failed
bios_save_metadata:
        mov #0x54, @#BIOS_ERROR
        br bios_save_failed
bios_save_full:
        mov #0x55, @#BIOS_ERROR
bios_save_failed:
        mov @#BIOS_MENU_SP, sp
        jmp bios_redraw

; Find a root slot before touching the disk. Save its sector/offset.
bios_allocate:
        clr r3
bios_root_sector:
        cmp r3, @#ROOT_COUNT
        bcc bios_save_full
        mov r3, r0
        add @#ROOT_START, r0
        mov r0, @#BIOS_NEW_ROOT
        jsr pc, relative_sector
        jsr pc, read_sector
        mov #BUFFER, r1
        mov #16., r2
bios_root_slot:
        tstb (r1)
        beq bios_root_slot_found
        cmpb (r1), #0xe5
        beq bios_root_slot_found
        add #32., r1
        sob r2, bios_root_slot
        inc r3
        br bios_root_sector
bios_root_slot_found:
        sub #BUFFER, r1
        mov r1, @#BIOS_NEW_ENTRY
        mov #2, r3
        mov #177777, @#BIOS_SAVE_DEST
bios_free_cluster:
        mov r3, r0
        swab r0
        bic #177400, r0
        add @#FAT_START, r0
        cmp r0, @#BIOS_SAVE_DEST
        beq bios_free_check
        mov r0, @#BIOS_SAVE_DEST
        jsr pc, relative_sector
        jsr pc, read_sector
bios_free_check:
        mov r3, r1
        bic #177400, r1
        asl r1
        add #BUFFER, r1
        tst (r1)
        beq bios_free_found
        inc r3
        mov @#CLUSTERS, r0
        add #2, r0
        cmp r3, r0
        bcs bios_free_cluster
        br bios_save_full
bios_free_found:
        mov r3, @#BIOS_FREE
        ; Refuse allocation if the second FAT already owns this cluster.
        cmp @#FAT_COPIES, #2
        bne bios_allocation_ready
        mov @#BIOS_SAVE_DEST, r0
        add @#FAT_SIZE, r0
        jsr pc, relative_sector
        jsr pc, read_sector
        mov @#BIOS_FREE, r1
        bic #177400, r1
        asl r1
        add #BUFFER, r1
        tst (r1)
        bne bios_save_metadata
bios_allocation_ready:
        mov @#BIOS_FREE, @#BIOS_CFG_CLUSTER
        mov @#BIOS_FREE, r0
bios_save_data:
        sub #2, r0
        mov @#SPC_SHIFT, r1
        beq bios_save_cluster_address
bios_save_cluster_shift:
        asl r0
        bcs bios_save_metadata
        sob r1, bios_save_cluster_shift
bios_save_cluster_address:
        add @#DATA_START, r0
        bcs bios_save_metadata
        cmp r0, @#TOTAL
        bcc bios_save_metadata
        jsr pc, relative_sector
        jsr pc, read_sector
        mov #BIOS_CFG, r1
        mov #BUFFER, r2
        mov #8., r3
bios_save_cfg_copy:
        mov (r1)+, (r2)+
        sob r3, bios_save_cfg_copy
        jsr pc, bios_write_verify
        tst @#BIOS_FOUND
        bne bios_saved
        clr @#BIOS_SAVE_COPY
bios_publish_fat:
        mov @#BIOS_FREE, r0
        swab r0
        bic #177400, r0
        add @#FAT_START, r0
        tst @#BIOS_SAVE_COPY
        beq bios_publish_fat_address
        add @#FAT_SIZE, r0
bios_publish_fat_address:
        jsr pc, relative_sector
        jsr pc, read_sector
        mov @#BIOS_FREE, r1
        bic #177400, r1
        asl r1
        add #BUFFER, r1
        mov #177777, (r1)
        jsr pc, bios_write_verify
        inc @#BIOS_SAVE_COPY
        cmp @#BIOS_SAVE_COPY, @#FAT_COPIES
        bcs bios_publish_fat
        ; Publish the root entry only after data and all FAT copies verify.
        mov @#BIOS_NEW_ROOT, r0
        jsr pc, relative_sector
        jsr pc, read_sector
        mov @#BIOS_NEW_ENTRY, r2
        add #BUFFER, r2
        mov r2, r3
        mov #16., r0
bios_clear_entry:
        clr (r3)+
        sob r0, bios_clear_entry
        mov #bios_settings_name, r1
        mov #11., r0
bios_copy_entry_name:
        movb (r1)+, (r2)+
        sob r0, bios_copy_entry_name
        movb #0x20, (r2)
        mov @#BIOS_NEW_ENTRY, r1
        add #BUFFER, r1
        mov @#BIOS_FREE, 26.(r1)
        mov #16., 28.(r1)
        jsr pc, bios_write_verify
bios_saved:
        mov @#BIOS_CFG_CLUSTER, r0
        sub #2, r0
        mov @#SPC_SHIFT, r1
        beq bios_saved_cluster
bios_saved_shift:
        asl r0
        sob r1, bios_saved_shift
bios_saved_cluster:
        add @#DATA_START, r0
        jsr pc, relative_sector
        jsr pc, read_sector
        mov #BUFFER, r1
        mov #BIOS_CFG, r2
        mov #8., r3
bios_verify_cfg_words:
        cmp (r1)+, (r2)+
        bne bios_save_metadata_jump
        sob r3, bios_verify_cfg_words
        jsr pc, bios_validate
        tst r0
        beq bios_save_metadata_jump
        jmp bios_apply
bios_save_metadata_jump:
        jmp bios_save_metadata

; Write/verify all 512 bytes, preserving data outside BK.SET.
bios_write_verify:
        mov #BUFFER, r1
        mov #SAVEBUF, r2
        mov #256., r3
bios_verify_snapshot:
        mov (r1)+, (r2)+
        sob r3, bios_verify_snapshot
        jsr pc, bios_write_sector
        tst r0
        bne bios_verify_read
        mov #0x52, @#BIOS_ERROR
        jmp bios_save_failed
bios_verify_read:
        jsr pc, read_sector
        mov #BUFFER, r1
        mov #SAVEBUF, r2
        mov #256., r3
bios_verify_sector:
        cmp (r1)+, (r2)+
        bne bios_verify_failed
        sob r3, bios_verify_sector
        rts pc
bios_verify_failed:
        mov #0x56, @#BIOS_ERROR
        jmp bios_save_failed

; CMD24. Busy ends only on the complete FF byte, including partial release.
bios_write_sector:
        mov r1, -(sp)
        mov r2, -(sp)
        mov r3, -(sp)
        mov r4, -(sp)
        mov r5, -(sp)
        mov @#SECTOR_LO, r2
        mov @#SECTOR_HI, r3
        cmp r3, @#PART_HI
        bcs bios_write_bad
        bhi bios_write_lower_ok
        cmp r2, @#PART_LO
        bcs bios_write_bad
bios_write_lower_ok:
        mov @#PART_LO, r0
        mov @#PART_HI, r1
        add @#PART_SIZE_LO, r0
        adc r1
        bcs bios_write_bad
        add @#PART_SIZE_HI, r1
        bcs bios_write_bad
        cmp r3, r1
        bhi bios_write_bad
        bcs bios_write_bounds_ok
        cmp r2, r0
        bcc bios_write_bad
bios_write_bounds_ok:
        tst @#SDHC
        bne bios_write_argument
        mov #9., r1
bios_write_byte_address:
        asl r2
        rol r3
        bcs bios_write_bad
        sob r1, bios_write_byte_address
bios_write_argument:
        mov #24., r0
        mov #0377, r4
        jsr pc, sd_command
        tst r0
        bne bios_write_bad
        mov #BUFFER, r1
        mov #512., r2
        jsr pc, bios_crc16
        mov r0, r4
        mov #0xfe, r0
        jsr pc, spi_byte
        mov #BUFFER, r1
        mov #512., r5
bios_write_bytes:
        movb (r1)+, r0
        jsr pc, spi_byte
        sob r5, bios_write_bytes
        mov r4, r0
        swab r0
        jsr pc, spi_byte
        mov r4, r0
        jsr pc, spi_byte
        mov #65535., r5
bios_write_response:
        mov #0377, r0
        jsr pc, spi_byte
        cmp r0, #0377
        bne bios_write_response_received
        sob r5, bios_write_response
        br bios_write_bad
bios_write_response_received:
        bic #177740, r0
        cmp r0, #5
        bne bios_write_bad
        mov #65535., r5
bios_write_busy:
        mov #0377, r0
        jsr pc, spi_byte
        cmp r0, #0377
        beq bios_write_good
        sob r5, bios_write_busy
bios_write_bad:
        clr r5
        br bios_write_release
bios_write_good:
        mov #1, r5
bios_write_release:
        mov #0x23, @#SPI_CTRL
        mov #0377, r0
        jsr pc, spi_byte
        mov r5, r0
        mov (sp)+, r5
        mov (sp)+, r4
        mov (sp)+, r3
        mov (sp)+, r2
        mov (sp)+, r1
        rts pc
