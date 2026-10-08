; uJ11 guest BIOS, loaded as TBIOS.BIN from partition 1 FAT16.
; The guest owns UI, countdown and redundant TERAK.SET records. SERV only
; validates profiles/FAT chains and transfers the preallocated settings data.
BIO equ 171020
BUFFER equ 040000
MAX_PROFILE equ @MAX_PROFILE@
        org 0
start:  mtps #340
        mov #20000,sp
        clr @#177572
        mov #200,@#177744
        mov #fonts,r0
        mov #161000,r1
        mov #96.,r3
char:   mov #10.,r2
glyph:  movb (r0)+,(r1)+
        sob r2,glyph
        add #6,r1
        sob r3,char
        clr @#177742
        clr saved
        clr sequence
        clr have_saved
        mov #1,lastslot
        clr r4
load:   mov r4,r0
        bis #01400,r0         ; 0300 hex: read one settings record to BUFFER
        jsr pc,service
        tst r0
        bne nextslot
        jsr pc,verify
        tst r0
        bne nextslot
        tst have_saved
        beq accept
        mov @#BUFFER+6.,r0
        sub sequence,r0
        ble nextslot          ; signed modular 16-bit generation comparison
accept: mov @#BUFFER+8.,saved
        mov @#BUFFER+6.,sequence
        mov r4,lastslot
        mov #1,have_saved
nextslot:
        inc r4
        cmp r4,#2
        bcs load
        mov #03000,r0        ; consume the long-Reset Setup request
        jsr pc,service
        mov r0,force_setup
        clr r4
scan:   mov r4,r0
        bis #0400,r0
        jsr pc,service
        asl r4
        clr available(r4)
        tst r0
        bne scan_next
        inc available(r4)
scan_next:
        asr r4
        inc r4
        cmp r4,#MAX_PROFILE
        blos scan
        mov saved,chosen
        mov chosen,r0
        asl r0
        tst available(r0)
        beq unavailable
        tst force_setup
        bne setup
        jsr pc,start_screen
        mov #10.,seconds
        mov #02400,r0        ; TIME, milliseconds modulo 65536
        jsr pc,service
        mov r0,timer
countdown:
        jsr pc,show_count
waitkey:
        tstb @#177560
        bpl tick
        mov @#177562,r0
        cmp r0,#177          ; PS/2 Delete, distinct from Backspace (010)
        beq setup
        cmp r0,#15
        beq boot_saved
tick:   mov @#BIO,r0
        sub timer,r0
        cmp r0,#1000.
        bcs waitkey
        add #1000.,timer
        dec seconds
        bne countdown
        br boot_saved
unavailable:
        mov #missing,message
        clr chosen
find_first:
        mov chosen,r0
        asl r0
        tst available(r0)
        bne setup
        inc chosen
        cmp chosen,#MAX_PROFILE
        blos find_first
        clr chosen
setup:  jsr pc,setup_screen
menu_key:
        tstb @#177560
        bpl menu_key
        mov @#177562,r0
        cmp r0,#15
        beq boot_chosen
        cmp r0,#35           ; F10, translated only in the TERAK BIOS profile
        beq save_boot
        cmp r0,#33
        beq boot_saved
        cmp r0,#32           ; native TERAK Up
        beq up
        cmp r0,#14           ; native TERAK Down
        beq down
        sub #60,r0
        cmp r0,#MAX_PROFILE
        bhi menu_key
        mov r0,r2
        asl r2
        tst available(r2)
        beq menu_key
        mov r0,chosen
        br setup
up:     mov #-1,r5
        br move_choice
down:   mov #1,r5
move_choice:
        mov #MAX_PROFILE+1,r3
move_next:
        add r5,chosen
        bpl positive
        mov #MAX_PROFILE,chosen
positive:
        cmp chosen,#MAX_PROFILE
        blos in_range
        clr chosen
in_range:
        mov chosen,r0
        asl r0
        tst available(r0)
        bne setup
        sob r3,move_next
        br menu_key
boot_saved:
        mov saved,chosen
boot_chosen:
        mov chosen,r0
        jsr pc,service
        tst r0
        bne unavailable
        reset
        jmp @#173000
save_boot:
        mov chosen,r0
        bis #0400,r0
        jsr pc,service
        tst r0
        bne unavailable
        mov #BUFFER,r0
        mov #256.,r2
zero:   clr (r0)+
        sob r2,zero
        mov #045524,@#BUFFER ; "TKST", version 1, sequence, profile, inverse
        mov #052123,@#BUFFER+2.
        mov #1,@#BUFFER+4.
        mov sequence,r0
        inc r0
        mov r0,@#BUFFER+6.
        mov chosen,@#BUFFER+8.
        mov chosen,r0
        com r0
        mov r0,@#BUFFER+10.
        jsr pc,sum_record
        neg r0
        mov r0,@#BUFFER+12.
        mov lastslot,r0
        neg r0
        inc r0
        bis #02000,r0        ; 0400 hex: write inactive record, verify readback
        jsr pc,service
        tst r0
        bne save_failed
        mov chosen,saved
        inc sequence
        neg lastslot
        inc lastslot
        mov #1,have_saved
        jmp boot_chosen
save_failed:
        mov #save_error,message
        jmp setup
service:
        mov r0,@#BIO
        mov @#BIO,r0
        rts pc
verify: mov #1,r0
        cmp @#BUFFER,#045524
        bne verified
        cmp @#BUFFER+2.,#052123
        bne verified
        cmp @#BUFFER+4.,#1
        bne verified
        cmp @#BUFFER+8.,#MAX_PROFILE
        bhi verified
        mov @#BUFFER+8.,r1
        com r1
        cmp r1,@#BUFFER+10.
        bne verified
        jsr pc,sum_record
verified:
        rts pc
sum_record:
        mov #BUFFER,r1
        mov #256.,r2
        clr r0
sum:    add (r1)+,r0
        sob r2,sum
        rts pc
; Text rows use native 128-byte stride; only the visible 80 columns are touched.
clear_screen:
        clr @#177744
        mov #160000,r1
        mov #24.,r4
clear_row:
        mov #80.,r2
clear_char:
        movb #40,(r1)+
        sob r2,clear_char
        add #48.,r1
        sob r4,clear_row
        mov #heading,r0
        mov #160203,r1        ; row 1, column 3
        jsr pc,print
        mov #line,r0
        mov #160402,r1        ; row 2
        jsr pc,print
        rts pc
start_screen:
        jsr pc,clear_screen
        mov #boot_label,r0
        mov #161003,r1        ; row 4
        jsr pc,print
        mov chosen,r0
        asl r0
        mov names(r0),r0
        jsr pc,print
        mov #default_label,r0
        tst have_saved
        beq defaulted
        mov #saved_label,r0
defaulted:
        mov #161203,r1
        jsr pc,print
        mov #memory,r0
        mov #161603,r1        ; row 7
        jsr pc,print
        mov #162403,r5        ; disks start at row 10
        jsr pc,summary
        mov #start_help,r0
        mov #165403,r1        ; row 22
        jsr pc,print
        mov #7,@#177744
        rts pc
show_count:
        mov #boot_count,r0
        mov #165003,r1        ; row 20, overwrite only countdown
        jsr pc,print
        mov seconds,r0
        jsr pc,decimal
        mov #count_suffix,r0
        jsr pc,print
        rts pc
setup_screen:
        jsr pc,clear_screen
        mov #setup_label,r0
        mov #160603,r1        ; row 3
        jsr pc,print
        mov #161003,r5        ; compact list starts at row 4
        clr r4
list:   mov r4,r0
        asl r0
        tst available(r0)
        beq list_next
        mov r5,r1
        movb #40,(r1)+
        cmp r4,chosen
        bne unselected
        movb #76,177777(r1)
unselected:
        mov r4,r0
        add #60,r0
        movb r0,(r1)+
        movb #40,(r1)+
        mov r4,r0
        asl r0
        mov names(r0),r0
        jsr pc,print
        add #200,r5
list_next:
        inc r4
        cmp r4,#MAX_PROFILE
        blos list
        mov #memory,r0
        mov #162603,r1        ; row 11
        jsr pc,print
        mov #163003,r5        ; disks start at row 12, at most 8 rows
        jsr pc,summary
        mov #menu_help,r0
        mov #165203,r1        ; row 21
        jsr pc,print
        mov #cancel_help,r0
        mov #165403,r1        ; row 22
        jsr pc,print
        tst message
        beq no_message
        mov message,r0
        mov #165603,r1        ; row 23
        jsr pc,print
no_message:
        mov #7,@#177744
        rts pc
summary:
        mov chosen,r0
        bis #0400,r0
        jsr pc,service
        tst r0
        bne summary_done
        clr r4
disk:   mov r4,r0
        bis #01000,r0
        jsr pc,service
        tst r0
        bpl disk_next
        mov r0,r3
        mov r5,r1
        mov #fd_label,r0
        cmp r4,#4
        bcs floppy
        mov #rl_label,r0
floppy: jsr pc,print
        mov r4,r0
        bic #177774,r0
        add #60,r0
        movb r0,(r1)+
        mov #part_label,r0
        jsr pc,print
        mov r3,r0
        bic #177740,r0
        jsr pc,decimal
        movb #40,(r1)+
        mov r3,r0
        swab r0
        bic #177760,r0
        asl r0
        mov formats(r0),r0
        jsr pc,print
        mov #rw_label,r0
        bit #040000,r3
        beq writable
        mov #ro_label,r0
writable:
        jsr pc,print
        add #200,r5
disk_next:
        inc r4
        cmp r4,#8.
        bcs disk
summary_done:
        rts pc
decimal:
        clr r2
tens:   cmp r0,#10.
        bcs ones
        sub #10.,r0
        inc r2
        br tens
ones:   tst r2
        beq digit
        add #60,r2
        movb r2,(r1)+
digit:  add #60,r0
        movb r0,(r1)+
        rts pc
print:  movb (r0)+,r2
        beq printed
        movb r2,(r1)+
        br print
printed:
        rts pc
        even
saved:  dw 0
chosen: dw 0
sequence: dw 0
lastslot: dw 1
have_saved: dw 0
force_setup: dw 0
seconds: dw 0
timer: dw 0
message: dw 0
available: dw 0,0,0,0,0,0
names:  dw default_name,ucsd,sj,fb,ram,hd
formats: dw fd,sssd,dssd,ssdd,dsdd,tssdd,tdsdd,rl01,rl02
heading: db "TERAK BIOS",0
line:   db "------------------------------------------------------------------------",0
boot_label: db "Boot configuration: ",0
saved_label: db "Saved selection from TERAK.SET",0
default_label: db "Default configuration: TERAK.CFG",0
setup_label: db "BIOS SETUP - Boot configuration",0
memory: db "Memory: @RAM_KIB@ KiB installed / @USABLE_KIB@ KiB usable",0
start_help: db "DEL: Enter BIOS Setup        ENTER: Boot now",0
boot_count: db "Boot in ",0
count_suffix: db " seconds. ",0
menu_help: db "Up/Down: Select   ENTER: Boot once   F10: Save and boot",0
cancel_help: db "ESC: Cancel changes and boot saved/default configuration",0
missing: db "Saved/default configuration unavailable. Select another.",0
save_error: db "Cannot save TERAK.SET. ENTER boots without saving.",0
default_name: db "Default (TERAK.CFG)",0
ucsd:   db "UCSD PASCAL II.0",0
sj:     db "RT-11 SJ",0
fb:     db "RT-11 FB",0
ram:    db "MEMORY TEST",0
hd:     db "RT-11 5.7 XM / RL",0
fd_label: db "FD",0
rl_label: db "RL",0
part_label: db "  P",0
rw_label: db " RW",0
ro_label: db " RO",0
fd:     db "UJ11FD1",0
sssd:   db "SSSD",0
dssd:   db "DSSD",0
ssdd:   db "SSDD",0
dsdd:   db "DSDD",0
tssdd:  db "TERAK-SSDD",0
tdsdd:  db "TERAK-DSDD",0
rl01:   db "RL01 5 MiB",0
rl02:   db "RL02 10 MiB",0
        even
fonts:
; @FONT@
