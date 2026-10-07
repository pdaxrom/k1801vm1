; uJ11 QB bootstrap, reconstructed from the calling convention and boot order
; in TERAK 50-0010-002 Rev E, E5. This is original code, not a dumped TERAK ROM.
; microasm11 -binary firmware/terak/qb_boot.asm qb_boot.bin
; Runs at 173000, relocates to 10000, installs the BPT read-only handler.
        org 10000
boot:   mtps #340
        mov #17000,sp
        mov #173000,r0
        mov #10000,r1
        mov #400,r2
copy:   mov (r0)+,(r1)+
        sob r2,copy
        jmp @#start
start:  mov #roh,@#14
        mov #340,@#16
        mov #roh,-(sp)           ; Native E5 softboot convention: saved ROH.
        mov #3,r4
        jsr pc,send
        mov #257,r4
        jsr pc,send
        mov #44,r4
        jsr pc,send
        clr r0
        mov #7,r3
        mov #17000,r2
        mov #100,r1
        bpt
        movb @#(17000+71.),r5
        bisb @#(17000+75.),r5
        bic #177770,r5
        asr r5
        mov r5,media_id
        movb #106,read_command
        movb #2,size_code
        movb #15.,end_track
        movb #27.,gap
        mov #400,sector_words
        mov #1,r0
        mov #3,r3
        mov #1000,r2
        mov #2000,r1
        bpt
        mov #5,r3
        bpt
        mov #7,r3
        bpt
        mov #1,r3
        clr r2
        bpt
        mov #10002,r4
        add media_id,r4
        mov r4,@#0
        clr pc

; R0=cylinder, R1=remaining words, R2=running buffer, R3=H:R.
; R0/R3 are preserved; R1/R2 advance by one sector or the remaining count.
roh:    mov r0,-(sp)
        mov r3,-(sp)
        tst r1
        beq return
        movb r0,cylinder
        movb r3,sector_id
        swab r3
        movb r3,head_id
        asl r3
        asl r3
        movb r3,unit_head
        mov #17,r4
        jsr pc,send
        mov r3,r4
        jsr pc,send
        mov r0,r4
        jsr pc,send
        jsr pc,wait_ready
        mov #10,r4
        jsr pc,send
        jsr pc,read_byte
        bic #37,r4
        cmp r4,#40
        bne failure
        jsr pc,read_byte
        mov sector_words,r0
        cmp r0,r1
        blos count_ready
        mov r1,r0
count_ready:
        mov r0,-(sp)
        mov r2,@#171016
        swab r2
        mov r2,@#171016
        swab r2
        asl r0
        dec r0
        bis #40000,r0
        mov r0,@#171014
        swab r0
        mov r0,@#171014
        mov #131,@#171012
        mov #read_command,r3
        mov #9.,r0
command_loop:
        movb (r3)+,r4
        jsr pc,send
        sob r0,command_loop
        jsr pc,read_byte
        bit #300,r4
        bne failure
        mov #6,r0
result_loop:
        jsr pc,read_byte
        sob r0,result_loop
        mov (sp)+,r0
        sub r0,r1
        asl r0
        add r0,r2
return: mov (sp)+,r3
        mov (sp)+,r0
        rti
failure:br failure
wait_ready:
        tstb @#171000
        bpl wait_ready
        rts pc
send:   jsr pc,wait_ready
        mov r4,@#171002
        rts pc
read_byte:
        jsr pc,wait_ready
        bit #100,@#171000
        beq read_byte
        mov @#171002,r4
        rts pc
read_command: db 6
unit_head:    db 0
cylinder:     db 0
head_id:      db 0
sector_id:    db 0
size_code:    db 0
end_track:    db 26.
gap:          db 7
data_length:  db 377
        even
sector_words: dw 100
media_id:     dw 1
        org 10776
        dw 234
