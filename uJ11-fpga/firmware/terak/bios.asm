; Optional uJ11 primary BIOS. Native TERAK font/text aperture and KSR/KDR.
; Configurations are TERAK1.CFG .. TERAK4.CFG on the first FAT16 partition.
; The private uJ11 selector at 171020 exists only in TERAK_BOOT=bios builds.
        org 0
start:  mtps #340
        mov #10000,sp
        clr @#177572
        mov #200,@#177744
        mov #fonts,r0
        mov #161000,r1          ; ASCII 32 * 16-byte native glyph stride
        mov #96.,r3
char:   mov #10.,r2
glyph:  movb (r0)+,(r1)+
        sob r2,glyph
        add #6,r1
        sob r3,char
        clr @#177742
        mov #7,@#177744
        mov #160000,r1
        mov #4000,r2
clear:  movb #40,(r1)+
        sob r2,clear
        mov #title,r0
        mov #160420,r1
        jsr pc,print
        mov #ucsd,r0
        mov #161020,r1
        jsr pc,print
        mov #sj,r0
        mov #161420,r1
        jsr pc,print
        mov #fb,r0
        mov #162020,r1
        jsr pc,print
        mov #ram,r0
        mov #162420,r1
        jsr pc,print
        mov #help,r0
        mov #163020,r1
        jsr pc,print
key:    tstb @#177560
        bpl key
        mov @#177562,r2
        sub #61,r2
        cmp r2,#3
        bhi key
        inc r2
        mov r2,@#171020
        tst @#171020
        beq boot
        mov #bad,r0
        mov #163420,r1
        jsr pc,print
        br key
boot:   reset
        jmp @#173000
print:  movb (r0)+,r2
        beq return
        movb r2,(r1)+
        br print
return: rts pc
title:  db "TERAK - SELECT BOOT",0
ucsd:   db "1  UCSD PASCAL II.0",0
sj:     db "2  RT-11 SJ",0
fb:     db "3  RT-11 FB",0
ram:    db "4  MEMORY TEST",0
help:   db "PRESS 1-4 TO BOOT",0
bad:    db "CONFIGURATION UNAVAILABLE",0
        even
fonts:
; @FONT@
