; Native VM1A resident SD/FAT16 bootstrap. Octal addresses, decimal suffix '.'.
SPI_DATA equ 177600
SPI_CTRL equ 177602
SPI_DIV equ 177604
STAGE equ 177612
BOOT_STATUS equ 177610
BUFFER equ 02000
STACK equ 010000
LOADER equ 020000
SDHC equ 01000
V2 equ 01002
PART_LO equ 01004
PART_HI equ 01006
PART_SIZE_LO equ 01010
PART_SIZE_HI equ 01012
TOTAL equ 01014
FAT_START equ 01016
ROOT_START equ 01020
ROOT_COUNT equ 01022
DATA_START equ 01024
CLUSTERS equ 01026
SPC equ 01030
SPC_SHIFT equ 01032
SECTOR_LO equ 01034
SECTOR_HI equ 01036
NAME_PTR equ 01040
ROOT_INDEX equ 01042
FILE_LEFT equ 01044
CLUSTER equ 01046
CLUSTER_SECTOR equ 01050
POSITION equ 01052
CHAIN_COUNT equ 01054
TEMP equ 01056

.org 100000
        jmp @#cold_start             ; 100000 cold entry
        jmp @#open_file              ; 100004 R0 -> eleven-byte 8.3 name, R0=size
        jmp @#get_byte               ; 100010 R0=next byte, preserves R1..R5
        jmp @#fail                   ; 100014 R0=error code
        jmp @#handoff                ; 100020 verified ROM handoff
        jmp @#show_stage             ; 100024 R0=stage/error, preserves R0..R5

cold_start:
        mov #STACK, sp
        mov #0340, r0
        mtps r0
        clr @#BOOT_STATUS
        jsr pc, splash
        mov #1, r0
        jsr pc, show_stage
        clr @#SDHC
        clr @#V2
        jsr pc, sd_init
        mov #2, r0
        jsr pc, show_stage
        clr @#SECTOR_LO
        clr @#SECTOR_HI
        jsr pc, read_sector
        cmp @#BUFFER+510., #0xaa55
        beq mbr_signature_ok
        jmp bad_mbr
mbr_signature_ok:
        movb @#BUFFER+450., r0
        cmpb r0, #6
        beq partition_type_ok
        cmpb r0, #0x0e
        beq partition_type_ok
        jmp bad_mbr
partition_type_ok:
        mov @#BUFFER+454., @#PART_LO
        mov @#BUFFER+456., @#PART_HI
        mov @#BUFFER+458., @#PART_SIZE_LO
        mov @#BUFFER+460., @#PART_SIZE_HI
        mov @#PART_LO, r0
        bis @#PART_HI, r0
        bne partition_nonzero
        jmp bad_mbr
partition_nonzero:
        clr r0
        jsr pc, relative_sector
        jsr pc, read_sector
        cmp @#BUFFER+510., #0xaa55
        bne bad_bpb_jump
        mov #BUFFER+11., r0
        jsr pc, little_word
        cmp r0, #512.
        bne bad_bpb_jump
        movb @#BUFFER+13., r0
        bic #177400, r0
        beq bad_bpb_jump
        cmp r0, #128.
        bhi bad_bpb_jump
        mov r0, @#SPC
        mov r0, r1
        dec r1
        bit r1, r0
        bne bad_bpb_jump
        clr @#SPC_SHIFT
spc_shift_loop:
        cmp r0, #1
        beq spc_shift_done
        asr r0
        inc @#SPC_SHIFT
        br spc_shift_loop
spc_shift_done:
        mov @#BUFFER+14., @#FAT_START
        beq bad_bpb_jump
        mov #BUFFER+19., r0
        jsr pc, little_word
        mov r0, @#TOTAL
        beq bad_bpb_jump         ; first implementation bounds volume to 65535 sectors
        tst @#PART_SIZE_HI
        bne partition_size_ok
        cmp @#TOTAL, @#PART_SIZE_LO
        bhi bad_bpb_jump
partition_size_ok:
        mov #BUFFER+17., r0
        jsr pc, little_word
        beq bad_bpb_jump
        cmp r0, #512.
        bhi bad_bpb_jump
        add #15., r0
        asr r0
        asr r0
        asr r0
        asr r0
        mov r0, @#ROOT_COUNT
        mov @#BUFFER+22., r0
        beq bad_bpb_jump
        cmp r0, #256.
        bhi bad_bpb_jump
        mov r0, @#TEMP
        movb @#BUFFER+16., r1
        cmpb r1, #1
        beq one_fat
        cmpb r1, #2
        bne bad_bpb_jump
        asl r0
one_fat:
        add @#FAT_START, r0
        bcs bad_bpb_jump
        mov r0, @#ROOT_START
        add @#ROOT_COUNT, r0
        bcs bad_bpb_jump
        mov r0, @#DATA_START
        cmp r0, @#TOTAL
        bcc bad_bpb_jump
        mov @#TOTAL, r0
        sub @#DATA_START, r0
        mov @#SPC_SHIFT, r1
        beq cluster_shift_done
cluster_shift_loop:
        clc
        ror r0
        sob r1, cluster_shift_loop
cluster_shift_done:
        cmp r0, #4085.
        bcs bad_bpb_jump
        cmp r0, #65525.
        bcc bad_bpb_jump
        mov r0, @#CLUSTERS
        ; FAT capacity: ceil((clusters+2)/256) sectors, using 32-bit addition.
        clr r1
        add #257., r0
        adc r1
        swab r0
        bic #177400, r0
        swab r1
        bis r1, r0
        cmp @#TEMP, r0
        bcs bad_bpb_jump
        br bpb_valid
bad_bpb_jump:
        jmp bad_bpb
bpb_valid:
        mov #3, r0
        jsr pc, show_stage
        mov #loader_name, r0
        jsr pc, open_file
        cmp r0, #4096.
        bhi bad_loader
        mov r0, r2
        mov #LOADER, r1
load_loader_loop:
        jsr pc, get_byte
        movb r0, (r1)+
        sob r2, load_loader_loop
        jmp @#LOADER
bad_loader:
        mov #0x13, r0
        jmp fail
bad_mbr:
        mov #0x11, r0
        jmp fail
bad_bpb:
        mov #0x12, r0
        jmp fail

; R0 = outgoing byte, R0 = incoming byte; only R0/flags are modified.
spi_byte:
        mov r1, -(sp)
        movb r0, @#SPI_DATA
        mov #65535., r1
spi_wait:
        bit #0200, @#SPI_CTRL
        bne spi_done
        sob r1, spi_wait
        mov #0x21, r0
        jmp fail
spi_done:
        movb @#SPI_DATA, r0
        bic #177400, r0
        mov (sp)+, r1
        rts pc

; FAT BPB words can be unaligned. Never issue a native odd-address word read.
little_word:
        mov r1, -(sp)
        mov r2, -(sp)
        mov r0, r1
        movb (r1)+, r0
        bic #177400, r0
        movb (r1), r2
        swab r2
        bic #0377, r2
        bis r2, r0
        mov (sp)+, r2
        mov (sp)+, r1
        tst r0
        rts pc

; R0=command, R2/R3=argument low/high, R4=CRC; returns R1 status in R0.
sd_command:
        mov r1, -(sp)
        mov r5, -(sp)
        mov r0, r5
        mov #0x23, @#SPI_CTRL
        mov #0377, r0
        jsr pc, spi_byte
        mov #0x21, @#SPI_CTRL
        mov r5, r0
        bis #0100, r0
        jsr pc, spi_byte
        mov r3, r0
        swab r0
        jsr pc, spi_byte
        mov r3, r0
        jsr pc, spi_byte
        mov r2, r0
        swab r0
        jsr pc, spi_byte
        mov r2, r0
        jsr pc, spi_byte
        mov r4, r0
        jsr pc, spi_byte
        mov #512., r1
sd_response:
        mov #0377, r0
        jsr pc, spi_byte
        bit #0200, r0
        beq sd_response_done
        sob r1, sd_response
        mov #0x22, r0
        jmp fail
sd_response_done:
        mov (sp)+, r5
        mov (sp)+, r1
        rts pc

sd_init:
        mov #29., @#SPI_DIV
        mov #0x23, @#SPI_CTRL
        mov #10., r5
sd_idle_clocks:
        mov #0377, r0
        jsr pc, spi_byte
        sob r5, sd_idle_clocks
        clr r2
        clr r3
        mov #0x95, r4
        clr r0
        jsr pc, sd_command
        cmp r0, #1
        bne sd_init_bad
        mov #0x1aa, r2
        mov #0x87, r4
        mov #8., r0
        jsr pc, sd_command
        cmp r0, #5
        beq sd_v1
        cmp r0, #1
        bne sd_init_bad
        mov #4, r5
sd_r7:
        mov #0377, r0
        jsr pc, spi_byte
        sob r5, sd_r7
        cmp r0, #0xaa
        bne sd_init_bad
        mov #1, @#V2
sd_v1:
        mov #1024., r5
sd_acmd_loop:
        clr r2
        clr r3
        mov #0377, r4
        mov #55., r0
        jsr pc, sd_command
        cmp r0, #1
        bhi sd_init_bad
        tst @#V2
        beq sd_acmd_argument
        mov #0x4000, r3
sd_acmd_argument:
        mov #41., r0
        jsr pc, sd_command
        tst r0
        beq sd_card_ready
        cmp r0, #1
        bne sd_init_bad
        sob r5, sd_acmd_loop
sd_init_bad:
        mov #0x23, r0
        jmp fail
sd_card_ready:
        clr r2
        clr r3
        mov #58., r0
        jsr pc, sd_command
        tst r0
        bne sd_init_bad
        mov #0377, r0
        jsr pc, spi_byte
        bit #0200, r0
        beq sd_init_bad
        bit #0100, r0
        beq sd_no_ccs
        mov #1, @#SDHC
sd_no_ccs:
        mov #3, r5
sd_ocr_rest:
        mov #0377, r0
        jsr pc, spi_byte
        sob r5, sd_ocr_rest
        tst @#SDHC
        bne sd_init_complete
        mov #512., r2
        clr r3
        mov #16., r0
        jsr pc, sd_command
        tst r0
        bne sd_init_bad
sd_init_complete:
        mov #0x23, @#SPI_CTRL
        mov #0377, r0
        jsr pc, spi_byte
        mov #1, @#SPI_DIV
        rts pc

; Relative sector in R0 -> checked absolute sector address in RAM.
relative_sector:
        mov @#PART_LO, @#SECTOR_LO
        mov @#PART_HI, @#SECTOR_HI
        add r0, @#SECTOR_LO
        adc @#SECTOR_HI
        bcc relative_valid
        jmp bad_mbr
relative_valid:
        rts pc

; Read sector into BUFFER. Preserves R1..R5.
read_sector:
        mov r1, -(sp)
        mov r2, -(sp)
        mov r3, -(sp)
        mov r4, -(sp)
        mov r5, -(sp)
        mov @#SECTOR_LO, r2
        mov @#SECTOR_HI, r3
        tst @#SDHC
        bne sd_read_argument
        mov #9., r1
sd_byte_address:
        asl r2
        rol r3
        bcs sd_read_bad
        sob r1, sd_byte_address
sd_read_argument:
        mov #17., r0
        mov #0377, r4
        jsr pc, sd_command
        tst r0
        bne sd_read_bad
        mov #65535., r5
sd_token:
        mov #0377, r0
        jsr pc, spi_byte
        cmp r0, #0xfe
        beq sd_copy_sector
        cmp r0, #0377
        bne sd_read_bad
        sob r5, sd_token
sd_read_bad:
        mov #0x24, r0
        jmp fail
sd_copy_sector:
        mov #BUFFER, r1
        mov #512., r5
sd_copy_loop:
        mov #0377, r0
        jsr pc, spi_byte
        movb r0, (r1)+
        sob r5, sd_copy_loop
        mov #0377, r0
        jsr pc, spi_byte
        mov #0377, r0
        jsr pc, spi_byte
        mov #0x23, @#SPI_CTRL
        mov #0377, r0
        jsr pc, spi_byte
        mov (sp)+, r5
        mov (sp)+, r4
        mov (sp)+, r3
        mov (sp)+, r2
        mov (sp)+, r1
        rts pc

; Root 8.3 lookup. Files are limited to 1..65535 bytes.
open_file:
        mov r1, -(sp)
        mov r2, -(sp)
        mov r3, -(sp)
        mov r4, -(sp)
        mov r5, -(sp)
        mov r0, @#NAME_PTR
        clr @#ROOT_INDEX
open_sector:
        mov @#ROOT_INDEX, r0
        cmp r0, @#ROOT_COUNT
        bcc file_bad
        add @#ROOT_START, r0
        jsr pc, relative_sector
        jsr pc, read_sector
        mov #BUFFER, r1
        mov #16., r5
open_entry:
        tstb (r1)
        beq file_bad
        cmpb (r1), #0xe5
        beq open_next
        bitb #0x18, 11.(r1)
        bne open_next
        mov @#NAME_PTR, r0
        mov r1, r2
        mov #11., r3
open_name:
        cmpb (r0)+, (r2)+
        bne open_next
        sob r3, open_name
        mov 26.(r1), r0
        jsr pc, validate_cluster
        mov r0, @#CLUSTER
        tst 30.(r1)
        bne file_bad
        mov 28.(r1), r0
        beq file_bad
        mov r0, @#FILE_LEFT
        clr @#CLUSTER_SECTOR
        clr @#CHAIN_COUNT
        mov #512., @#POSITION
        mov (sp)+, r5
        mov (sp)+, r4
        mov (sp)+, r3
        mov (sp)+, r2
        mov (sp)+, r1
        rts pc
open_next:
        add #32., r1
        sob r5, open_entry
        inc @#ROOT_INDEX
        jmp open_sector
file_bad:
        mov #0x31, r0
        jmp fail
validate_cluster:
        cmp r0, #2
        bcs file_bad
        mov @#CLUSTERS, r4
        add #2, r4
        cmp r0, r4
        bcc file_bad
        rts pc

get_byte:
        mov r1, -(sp)
        mov r2, -(sp)
        mov r3, -(sp)
        mov r4, -(sp)
        mov r5, -(sp)
        tst @#FILE_LEFT
        beq file_bad
        cmp @#POSITION, #512.
        bcs get_buffer_byte
        mov @#CLUSTER_SECTOR, r0
        cmp r0, @#SPC
        bcs get_cluster_sector
        mov @#CLUSTER, r0
        swab r0
        bic #177400, r0
        add @#FAT_START, r0
        jsr pc, relative_sector
        jsr pc, read_sector
        mov @#CLUSTER, r1
        bic #177400, r1
        asl r1
        add #BUFFER, r1
        mov (r1), r0
        jsr pc, validate_cluster
        mov r0, @#CLUSTER
        clr @#CLUSTER_SECTOR
        inc @#CHAIN_COUNT
        mov @#CHAIN_COUNT, r0
        cmp r0, @#CLUSTERS
        bcc file_bad
get_cluster_sector:
        mov @#CLUSTER, r0
        sub #2, r0
        mov @#SPC_SHIFT, r1
        beq get_relative
get_shift:
        asl r0
        bcs file_bad
        sob r1, get_shift
get_relative:
        add @#DATA_START, r0
        bcs file_bad
        add @#CLUSTER_SECTOR, r0
        bcs file_bad
        cmp r0, @#TOTAL
        bcc file_bad
        jsr pc, relative_sector
        jsr pc, read_sector
        inc @#CLUSTER_SECTOR
        clr @#POSITION
get_buffer_byte:
        mov @#POSITION, r1
        add #BUFFER, r1
        movb (r1), r0
        bic #177400, r0
        inc @#POSITION
        dec @#FILE_LEFT
        mov (sp)+, r5
        mov (sp)+, r4
        mov (sp)+, r3
        mov (sp)+, r2
        mov (sp)+, r1
        rts pc

handoff:
        ; Execute commit from RAM: the resident ROM disappears at lock.
        mov #012737, @#0400
        mov #0xa55a, @#0402
        mov #177636, @#0404
        mov #000137, @#0406
        mov #100000, @#0410
        mov #0340, r0
        mtps r0
        jmp @#0400
fail:
        bis #0xe000, r0
        jsr pc, show_stage
        mov #0x23, @#SPI_CTRL
fail_loop:
        br fail_loop
loader_name:
        db "LOADERBKBIN"
        even

; Minimal resident PAL status. MONIT clears it after successful handoff.
splash:
        mov #040000, r1
        mov #splash_rows, r3
        mov #8., r2
splash_line:
        mov #6, r4
        mov r1, r5
splash_character:
        movb (r3)+, (r5)+
        sob r4, splash_character
        add #64., r1
        sob r2, splash_line
        rts pc
show_stage:
        mov r0, @#STAGE
        mov r0, -(sp)
        mov r1, -(sp)
        mov r2, -(sp)
        mov r3, -(sp)
        mov r4, -(sp)
        mov r0, r3
        asr r3
        asr r3
        asr r3
        asr r3
        bic #177760, r3
        mov r0, r4
        bic #177760, r4
        asl r3
        asl r3
        asl r3
        asl r4
        asl r4
        asl r4
        add #hex_font, r3
        add #hex_font, r4
        mov #040010, r1
        mov #8., r2
stage_line:
        movb (r3)+, (r1)
        movb (r4)+, 1(r1)
        add #64., r1
        sob r2, stage_line
        mov (sp)+, r4
        mov (sp)+, r3
        mov (sp)+, r2
        mov (sp)+, r1
        mov (sp)+, r0
        rts pc

hex_font:
        db 0x0e,0x11,0x19,0x15,0x13,0x11,0x0e,0x00
        db 0x04,0x06,0x04,0x04,0x04,0x04,0x0e,0x00
        db 0x0e,0x11,0x10,0x08,0x04,0x02,0x1f,0x00
        db 0x0f,0x10,0x10,0x0e,0x10,0x10,0x0f,0x00
        db 0x08,0x0c,0x0a,0x09,0x1f,0x08,0x08,0x00
        db 0x1f,0x01,0x01,0x0f,0x10,0x10,0x0f,0x00
        db 0x0e,0x01,0x01,0x0f,0x11,0x11,0x0e,0x00
        db 0x1f,0x10,0x08,0x04,0x02,0x02,0x02,0x00
        db 0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e,0x00
        db 0x0e,0x11,0x11,0x1e,0x10,0x10,0x0e,0x00
        db 0x0e,0x11,0x11,0x1f,0x11,0x11,0x11,0x00
        db 0x0f,0x11,0x11,0x0f,0x11,0x11,0x0f,0x00
        db 0x1e,0x01,0x01,0x01,0x01,0x01,0x1e,0x00
        db 0x0f,0x11,0x11,0x11,0x11,0x11,0x0f,0x00
        db 0x1f,0x01,0x01,0x0f,0x01,0x01,0x1f,0x00
        db 0x1f,0x01,0x01,0x0f,0x01,0x01,0x01,0x00
splash_rows:
        db 0x1e,0x0f,0x00,0x0f,0x0e,0x11
        db 0x01,0x11,0x00,0x11,0x11,0x1b
        db 0x01,0x11,0x00,0x11,0x11,0x15
        db 0x0e,0x11,0x00,0x0f,0x11,0x15
        db 0x10,0x11,0x00,0x05,0x11,0x11
        db 0x10,0x11,0x00,0x09,0x11,0x11
        db 0x0f,0x0f,0x00,0x11,0x0e,0x11
        db 0x00,0x00,0x00,0x00,0x00,0x00
