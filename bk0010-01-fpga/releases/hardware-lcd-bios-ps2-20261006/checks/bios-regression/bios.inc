; Native VM1 BIOS routines. Constants are in defs.inc.
bios_defaults:
        clr @#BIOS_VALID
        clr @#BIOS_SD_READY
        clr @#BIOS_ERROR
        clr @#BIOS_FOUND
        clr @#BIOS_CURSOR
        mov #bios_default_cfg, r1
        mov #BIOS_CFG, r2
        mov #8., r3
bios_defaults_copy:
        mov (r1)+, (r2)+
        sob r3, bios_defaults_copy
        rts pc

bios_setup:
        mov #bios_settings_name, r0
        jsr pc, try_open_file
        mov @#ROOT_FOUND, @#BIOS_FOUND
        mov @#ROOT_REL, @#BIOS_CFG_ROOT
        mov @#ROOT_ENTRY, @#BIOS_CFG_ENTRY
        mov @#CLUSTER, @#BIOS_CFG_CLUSTER
        mov @#FILE_ATTR, @#BIOS_CFG_ATTR
        mov r0, @#BIOS_CFG_SIZE
        cmp r0, #16.
        bne bios_settings_bad
        mov #BIOS_CFG, r1
        mov #16., r2
bios_settings_read:
        jsr pc, get_byte
        movb r0, (r1)+
        sob r2, bios_settings_read
        jsr pc, bios_validate
        tst r0
        beq bios_settings_bad
        mov #1, @#BIOS_VALID
        br bios_settings_ready
bios_settings_bad:
        mov #bios_default_cfg, r1
        mov #BIOS_CFG, r2
        mov #8., r3
bios_bad_defaults:
        mov (r1)+, (r2)+
        sob r3, bios_bad_defaults
bios_settings_ready:
        mov #BIOS_CFG, r1
        mov #BIOS_ORIGINAL, r2
        mov #8., r3
bios_original_copy:
        mov (r1)+, (r2)+
        sob r3, bios_original_copy
        tst @#BIOS_VALID
        beq bios_menu
        mov #0x10, r0
        jsr pc, show_stage
        jsr pc, bios_draw
        mov #bios_notice, r1
        mov #076000, r5
        jsr pc, bios_puts
        mov #500., @#BIOS_TICKS
        tst @#BIOS_TICK
bios_wait:
        jsr pc, bios_key
        cmp r0, #030
        beq bios_menu
        ; The countdown already displays the menu and arrow controls. Start
        ; editing on the first arrow instead of discarding it and auto-booting.
        cmp r0, #021
        bcs bios_wait_enter
        cmp r0, #024
        bhi bios_wait_enter
        br bios_menu_first
bios_wait_enter:
        cmp r0, #012
        bne bios_far_1
        jmp bios_apply
bios_far_1:
        tst @#BIOS_TICK
        beq bios_wait
        dec @#BIOS_TICKS
        bne bios_wait
        jmp bios_apply

bios_menu:
        clr r0
bios_menu_first:
        mov sp, @#BIOS_MENU_SP
        clr @#BIOS_CURSOR
        mov r0, -(sp)
        mov #0x11, r0
        jsr pc, show_stage
        mov (sp)+, r0
        tst r0
        bne bios_dispatch
bios_redraw:
        jsr pc, bios_draw
bios_menu_key:
        jsr pc, bios_key
        tst r0
        beq bios_menu_key
bios_dispatch:
        cmp r0, #033
        bne bios_far_3
        jmp bios_cancel
bios_far_3:
        cmp r0, #023
        beq bios_up
        cmp r0, #024
        beq bios_down
        ; BIOS arrows use distinct control codes; native BK keys resume at lock.
        cmp r0, #021
        beq bios_left
        cmp r0, #022
        beq bios_right
        cmp r0, #012
        bne bios_far_4
        jmp bios_enter
bios_far_4:
        br bios_menu_key
bios_up:
        tst @#BIOS_CURSOR
        bne bios_up_decrement
        mov #10., @#BIOS_CURSOR
bios_up_decrement:
        dec @#BIOS_CURSOR
        br bios_redraw
bios_down:
        inc @#BIOS_CURSOR
        cmp @#BIOS_CURSOR, #10.
        bcs bios_redraw
        clr @#BIOS_CURSOR
        br bios_redraw
bios_left:
        mov #1, r4
        br bios_edit
bios_right:
        clr r4
bios_edit:
        mov @#BIOS_CURSOR, r3
        tst r3
        bne bios_edit_frequency
        movb @#BIOS_CFG+9., r0
        bic #177400, r0
        tst r4
        beq bios_model_next
        add #3, r0
bios_model_next:
        inc r0
        cmp r0, #5
        bcs bios_model_ok
        sub #5, r0
bios_model_ok:
        movb r0, @#BIOS_CFG+9.
        clr r1
        cmp r0, #3
        bcs bios_default_frequency
        mov #1, r1
bios_default_frequency:
        movb r1, @#BIOS_CFG+10.
        clrb @#BIOS_CFG+11.
        clrb @#BIOS_CFG+12.
        clrb @#BIOS_CFG+13.
        cmp r0, #0
        beq bios_model_disks
        cmp r0, #3
        beq bios_edit_guard_0
        jmp bios_edited
bios_edit_guard_0:
bios_model_disks:
        tstb @#BIOS_LOGICAL+4
        beq bios_model_primary_disks
        movb #5, @#BIOS_CFG+11.
        movb #1, @#BIOS_CFG+13.
        tstb @#BIOS_LOGICAL+20.
        bne bios_edit_guard_1
        jmp bios_edited
bios_edit_guard_1:
        movb #0x65, @#BIOS_CFG+11.
        movb #3, @#BIOS_CFG+13.
        cmp r0, #3
        beq bios_edit_guard_2
        jmp bios_edited
bios_edit_guard_2:
        movb #0x56, @#BIOS_CFG+11.
        jmp bios_edited
bios_model_primary_disks:
        tstb @#BIOS_MBR+20.
        beq bios_default_disk_b
        tst @#BIOS_MBR+30.
        bne bios_default_disk_a_ok
        cmp @#BIOS_MBR+28., #1600.
        bcs bios_default_disk_b
bios_default_disk_a_ok:
        movb #2, @#BIOS_CFG+11.
bios_default_disk_b:
        tstb @#BIOS_MBR+36.
        bne bios_edit_guard_3
        jmp bios_edited
bios_edit_guard_3:
        tst @#BIOS_MBR+46.
        bne bios_default_disk_b_ok
        cmp @#BIOS_MBR+44., #1600.
        bcc bios_edit_guard_4
        jmp bios_edited
bios_edit_guard_4:
bios_default_disk_b_ok:
        bisb #0x30, @#BIOS_CFG+11.
        jmp bios_edited
bios_edit_frequency:
        cmp r3, #1
        bne bios_edit_video
        movb @#BIOS_CFG+10., r0
        bic #177770, r0
        tst r4
        beq bios_frequency_next
        add #5, r0
bios_frequency_next:
        inc r0
        cmp r0, #7
        bcs bios_frequency_ok
        sub #7, r0
bios_frequency_ok:
        bicb #7, @#BIOS_CFG+10.
        bisb r0, @#BIOS_CFG+10.
        jmp bios_edited
bios_edit_video:
        cmp r3, #2
        bne bios_edit_drive
        movb @#BIOS_CFG+10., r0
        mov #010, r1
        xor r1, r0
        movb r0, @#BIOS_CFG+10.
        jmp bios_edited
bios_edit_drive:
        cmp r3, #7
        bcs bios_edit_guard_5
        jmp bios_edited
bios_edit_guard_5:
        sub #3, r3
        mov r3, r0
        jsr pc, bios_drive_get
        ; values OFF, PART 2..6 RW/RO cycle in both directions.
        tst r0
        beq bios_drive_disabled
        asl r0
        sub #3, r0
        add r2, r0
bios_drive_disabled:
        tst r4
        beq bios_drive_next
        add #9., r0
bios_drive_next:
        inc r0
        cmp r0, #11.
        bcs bios_drive_value
        sub #11., r0
bios_drive_value:
        clr r2
        tst r0
        beq bios_drive_store
        mov r0, r2
        dec r2
        bic #177776, r2
        inc r0
        asr r0
        inc r0
bios_drive_store:
        cmp r0, #4
        bne bios_drive_store_data
        cmpb @#BIOS_MBR+52., #5
        beq bios_drive_skip_container
        cmpb @#BIOS_MBR+52., #017
        beq bios_drive_skip_container
        cmpb @#BIOS_MBR+52., #0205
        bne bios_drive_store_data
bios_drive_skip_container:
        asl r0
        sub #3, r0
        add r2, r0
        jmp bios_drive_disabled
bios_drive_store_data:
        jsr pc, bios_drive_set
bios_edited:
        clr @#BIOS_ERROR
        jmp bios_redraw
bios_enter:
        cmp @#BIOS_CURSOR, #7
        bne bios_far_5
        jmp bios_save
bios_far_5:
        cmp @#BIOS_CURSOR, #8.
        beq bios_boot_edit
        cmp @#BIOS_CURSOR, #9.
        beq bios_cancel
        clr r4
        jmp bios_edit
bios_cancel:
        mov #BIOS_ORIGINAL, r1
        mov #BIOS_CFG, r2
        mov #8., r3
bios_cancel_copy:
        mov (r1)+, (r2)+
        sob r3, bios_cancel_copy
bios_boot_edit:
        jsr pc, bios_cfg_crc
        jsr pc, bios_validate
        tst r0
        bne bios_apply
        mov #0x51, @#BIOS_ERROR
        jmp bios_redraw
bios_apply:
        clr r0
        movb @#BIOS_CFG+9., r0
        mov r0, @#BIOS_MODEL
        clr r0
        movb @#BIOS_CFG+10., r0
        mov r0, @#BIOS_CONFIG
        jsr pc, bios_publish_disks
        rts pc

; Publish only validated raw partitions; FAT16 is never a drive descriptor.
bios_publish_disks:
        clr @#BIOS_DISKS
        clr @#BIOS_DISK_INDEX
        clr r3
bios_publish_drive:
        mov r3, r0
        jsr pc, bios_drive_get
        mov #4, r2
        tst r0
        bne bios_publish_extent
bios_publish_absent:
        clr @#BIOS_DISK_DATA
        sob r2, bios_publish_absent
        br bios_publish_next
bios_publish_extent:
        jsr pc, bios_partition_address
        add #8., r0
bios_publish_word:
        mov (r0)+, @#BIOS_DISK_DATA
        sob r2, bios_publish_word
bios_publish_next:
        inc r3
        cmp r3, #4
        bcs bios_publish_drive
        mov @#PART_LO, @#BIOS_DISK_DATA
        mov @#PART_HI, @#BIOS_DISK_DATA
        mov @#PART_SIZE_LO, @#BIOS_DISK_DATA
        mov @#PART_SIZE_HI, @#BIOS_DISK_DATA
        mov @#CARD_LO, @#BIOS_DISK_DATA
        mov @#CARD_HI, @#BIOS_DISK_DATA
        clr r0
        movb @#BIOS_CFG+13., r0
        swab r0
        bis #0100000, r0
        bis @#SDHC, r0
        mov r0, @#BIOS_DISKS
bios_publish_wait:
        tst @#BIOS_DISKS
        bpl bios_publish_wait
        rts pc

; Nonblocking keyboard read. Returns 0 with no key.
bios_key:
        clr r0
        bit #0200, @#KBD_STATUS
        beq bios_no_key
        mov @#KBD_DATA, r0
bios_no_key:
        rts pc

; CRC16-CCITT, initial 0. R1=data, R2=length, returns R0, preserves R3..R5.
bios_crc16:
        mov r3, -(sp)
        mov r4, -(sp)
        clr r0
bios_crc_byte:
        movb (r1)+, r3
        swab r3
        bic #0377, r3
        xor r3, r0
        mov #8., r4
bios_crc_bit:
        asl r0
        bcc bios_crc_shifted
        mov #0x1021, r3
        xor r3, r0
bios_crc_shifted:
        sob r4, bios_crc_bit
        sob r2, bios_crc_byte
        mov (sp)+, r4
        mov (sp)+, r3
        rts pc
bios_cfg_crc:
        mov #BIOS_CFG, r1
        mov #14., r2
        jsr pc, bios_crc16
        mov r0, @#BIOS_CFG+14.
        rts pc

bios_validate:
        cmp @#BIOS_CFG, #0x4b42
        bne bios_invalid
        cmp @#BIOS_CFG+2, #0x4942
        bne bios_invalid
        cmp @#BIOS_CFG+4, #0x534f
        bne bios_invalid
        tst @#BIOS_CFG+6
        bne bios_invalid
        cmpb @#BIOS_CFG+8., #1
        bne bios_invalid
        cmpb @#BIOS_CFG+9., #4
        bhi bios_invalid
        movb @#BIOS_CFG+10., r0
        bic #177770, r0
        cmp r0, #6
        bhi bios_invalid
        bitb #0360, @#BIOS_CFG+10.
        bne bios_invalid
        bitb #0360, @#BIOS_CFG+13.
        bne bios_invalid
        mov #BIOS_CFG, r1
        mov #14., r2
        jsr pc, bios_crc16
        cmp r0, @#BIOS_CFG+14.
        bne bios_invalid
        clr r3
        clr r4
bios_validate_drive:
        mov r3, r0
        jsr pc, bios_drive_get
        tst r0
        beq bios_validate_next
        cmp r0, #2
        bcs bios_invalid
        cmp r0, #6
        bhi bios_invalid
        mov #1, r2
        mov r0, r1
bios_partition_mask:
        asl r2
        sob r1, bios_partition_mask
        bit r2, r4
        bne bios_invalid
        bis r2, r4
        mov #1, r2
        mov r3, r1
        beq bios_drive_mask_ok
bios_drive_mask_shift:
        asl r2
        sob r1, bios_drive_mask_shift
bios_drive_mask_ok:
        movb @#BIOS_CFG+9., r1
        bic #177400, r1
        add #bios_drive_masks, r1
        bitb r2, (r1)
        beq bios_invalid
        ; A selected disk must be a present data partition of >=1600 sectors.
        jsr pc, bios_partition_address
        tstb 4(r0)
        beq bios_invalid
        cmpb 4(r0), #5
        beq bios_invalid
        cmpb 4(r0), #017
        beq bios_invalid
        cmpb 4(r0), #0205
        beq bios_invalid
        mov 8.(r0), r1
        bis 10.(r0), r1
        beq bios_invalid
        tst 14.(r0)
        bne bios_validate_next
        cmp 12.(r0), #1600.
        bcs bios_invalid
bios_validate_next:
        inc r3
        cmp r3, #4
        bcs bios_validate_drive
        mov #1, r0
        rts pc
bios_invalid:
        clr r0
        rts pc

; R0=drive 0..3 -> R0=partition, R2=readonly. Preserves R3..R5.
bios_drive_get:
        mov r1, -(sp)
        mov r0, r1
        asr r1
        add #BIOS_CFG+11., r1
        movb (r1), r1
        bit #1, r0
        beq bios_drive_low
        asr r1
        asr r1
        asr r1
        asr r1
bios_drive_low:
        movb @#BIOS_CFG+13., r2
        tst r0
        beq bios_readonly_ready
bios_readonly_shift:
        asr r2
        sob r0, bios_readonly_shift
bios_readonly_ready:
        bic #177776, r2
        mov r1, r0
        bic #177760, r0
        mov (sp)+, r1
        rts pc

; R3=drive, R0=partition, R2=readonly. Preserves R4.
bios_drive_set:
        mov r3, r1
        asr r1
        add #BIOS_CFG+11., r1
        bit #1, r3
        beq bios_drive_set_low
        asl r0
        asl r0
        asl r0
        asl r0
        bicb #0360, (r1)
        br bios_drive_set_part
bios_drive_set_low:
        bicb #017, (r1)
bios_drive_set_part:
        bisb r0, (r1)
        mov #1, r0
        mov r3, r1
        beq bios_drive_set_ro
bios_drive_set_mask:
        asl r0
        sob r1, bios_drive_set_mask
bios_drive_set_ro:
        bicb r0, @#BIOS_CFG+13.
        tst r2
        beq bios_drive_set_done
        bisb r0, @#BIOS_CFG+13.
bios_drive_set_done:
        rts pc

; Own PAL UI: 32 columns, 16 rows; glyphs are doubled horizontally/vertically.
bios_puts:
        movb (r1)+, r0
        beq bios_puts_done
        jsr pc, bios_putc
        br bios_puts
bios_puts_done:
        rts pc
bios_putc:
        mov r1, -(sp)
        mov r2, -(sp)
        mov r3, -(sp)
        mov r4, -(sp)
        bic #177400, r0
        cmp r0, #32.
        bcs bios_putc_blank
        cmp r0, #96.
        bcs bios_putc_valid
bios_putc_blank:
        mov #32., r0
bios_putc_valid:
        sub #32., r0
        asl r0
        asl r0
        asl r0
        asl r0
        add #bios_font, r0
        mov r5, r2
        mov #8., r3
bios_glyph_row:
        mov (r0)+, r4
        mov r4, (r2)
        mov r4, 64.(r2)
        add #128., r2
        sob r3, bios_glyph_row
        add #2, r5
        mov (sp)+, r4
        mov (sp)+, r3
        mov (sp)+, r2
        mov (sp)+, r1
        rts pc
bios_hex4:
        mov r0, -(sp)
        mov r2, -(sp)
        mov r0, r2
        mov #4, r1
bios_hex_digit:
        clr r0
        mov #4, -(sp)
bios_hex_bits:
        asl r2
        rol r0
        dec (sp)
        bne bios_hex_bits
        tst (sp)+
        add #bios_hex_digits, r0
        movb (r0), r0
        jsr pc, bios_putc
        sob r1, bios_hex_digit
        mov (sp)+, r2
        mov (sp)+, r0
        rts pc
bios_draw:
        mov #040000, r1
        mov #8192., r2
bios_clear_screen:
        clr (r1)+
        sob r2, bios_clear_screen
        mov #bios_title, r1
        mov #040000, r5
        jsr pc, bios_puts
        movb @#BIOS_CFG+9., r0
        bic #177400, r0
        asl r0
        add #bios_model_labels, r0
        mov (r0), r1
        mov #042004, r5
        jsr pc, bios_puts
        mov #bios_cpu_label, r1
        mov #044004, r5
        jsr pc, bios_puts
        movb @#BIOS_CFG+10., r0
        bic #177770, r0
        asl r0
        add #bios_frequencies, r0
        mov (r0), r1
        jsr pc, bios_puts
        mov #bios_mono, r1
        bitb #010, @#BIOS_CFG+10.
        beq bios_video_label
        mov #bios_color, r1
bios_video_label:
        mov #046004, r5
        jsr pc, bios_puts
        clr r3
        mov #050004, r5
bios_draw_drive:
        mov r3, r0
        add #65., r0
        jsr pc, bios_putc
        mov #bios_part_label, r1
        jsr pc, bios_puts
        mov r3, r0
        jsr pc, bios_drive_get
        add #48., r0
        jsr pc, bios_putc
        mov #bios_rw, r1
        tst r2
        beq bios_drive_permission
        mov #bios_ro, r1
bios_drive_permission:
        jsr pc, bios_puts
        add #1024.-24., r5
        inc r3
        cmp r3, #4
        bcs bios_draw_drive
        mov #bios_save_label, r1
        mov #060004, r5
        jsr pc, bios_puts
        mov #bios_boot_label, r1
        mov #062004, r5
        jsr pc, bios_puts
        mov #bios_cancel_label, r1
        mov #064004, r5
        jsr pc, bios_puts
        mov #bios_controls, r1
        mov #074000, r5
        jsr pc, bios_puts
        mov @#BIOS_CURSOR, r0
        inc r0
        mov #10., r2
bios_cursor_offset:
        asl r0
        sob r2, bios_cursor_offset
        add #040000, r0
        mov r0, r5
        mov #62., r0
        jsr pc, bios_putc
        tst @#BIOS_ERROR
        beq bios_draw_partition
        mov #bios_error_label, r1
        mov #076000, r5
        jsr pc, bios_puts
        mov @#BIOS_ERROR, r0
        jsr pc, bios_hex4
bios_draw_partition:
        mov @#BIOS_CURSOR, r0
        sub #3, r0
        cmp r0, #4
        bcc bios_draw_done
        jsr pc, bios_drive_get
        tst r0
        beq bios_draw_done
        jsr pc, bios_partition_address
        mov r0, r4
        mov #bios_lba, r1
        mov #070000, r5
        jsr pc, bios_puts
        mov 10.(r4), r0
        jsr pc, bios_hex4
        mov 8.(r4), r0
        jsr pc, bios_hex4
        mov #bios_size, r1
        jsr pc, bios_puts
        mov 14.(r4), r0
        jsr pc, bios_hex4
        mov 12.(r4), r0
        jsr pc, bios_hex4
        mov #bios_geometry, r1
        mov #072000, r5
        jsr pc, bios_puts
bios_draw_done:
        rts pc

bios_header_contract:
        ; Loader has validated model/header CRC; compare all physical segments.
        movb @#BIOS_CFG+9., r0
        bic #177400, r0
        asl r0
        add #bios_contract_labels, r0
        mov (r0), r0
        cmp 14.(r1), (r0)+
        bne bios_contract_bad
        cmp 20.(r1), (r0)+
        bne bios_contract_bad
        cmp 16.(r1), (r0)+
        bne bios_contract_bad
        mov 14.(r1), r3
        add #32., r1
bios_contract_segment:
        mov #6, r2
bios_contract_word:
        cmp (r1)+, (r0)+
        bne bios_contract_bad
        sob r2, bios_contract_word
        add #4, r1
        sob r3, bios_contract_segment
        mov #1, r0
        rts pc
bios_contract_bad:
        clr r0
        rts pc

bios_settings_name:
        db "BK      SET"
        even
bios_default_cfg:
        db "BKBIOS",0,0,1,2,0,0,0,0
        dw 0
bios_drive_masks:
        .include "../../build/model-drive-masks.inc"
bios_hex_digits:
        db "0123456789ABCDEF"
bios_model_labels:
        dw bios_model0,bios_model1,bios_model2,bios_model3,bios_model4
bios_model0: db "BK0010-01 + A16M",0
bios_model1: db "BK0010-01 + FOCAL",0
bios_model2: db "BK0010-01 + BASIC86",0
bios_model3: db "BK0011M BASIC + FDD",0
bios_model4: db "BK0011M + MSTD",0
        even
bios_frequencies:
        dw bios_freq0,bios_freq1,bios_freq2,bios_freq3,bios_freq4,bios_freq5,bios_freq6
bios_freq0: db "03 MHZ",0
bios_freq1: db "04 MHZ",0
bios_freq2: db "06 MHZ",0
bios_freq3: db "08 MHZ",0
bios_freq4: db "12 MHZ",0
bios_freq5: db "16 MHZ",0
bios_freq6: db "24 MHZ",0
bios_title: db "BK FPGA BIOS",0
bios_cpu_label: db "CPU: ",0
bios_mono: db "VIDEO: MONO",0
bios_color: db "VIDEO: COLOR",0
bios_part_label: db ": PART ",0
bios_rw: db " RW",0
bios_ro: db " RO",0
bios_save_label: db "SAVE AND BOOT",0
bios_boot_label: db "BOOT WITHOUT SAVE",0
bios_cancel_label: db "EXIT AND BOOT",0
bios_controls: db "ARROWS: EDIT  ENTER: SELECT",0
bios_notice: db "10 SEC  BACKSPACE: SETUP",0
bios_error_label: db "ERROR ",0
bios_lba: db "LBA ",0
bios_size: db " SZ ",0
bios_geometry: db "80 TR 2 HEADS 10 SECT 512 BYTES",0
        even

INCLUDE "save.inc"
