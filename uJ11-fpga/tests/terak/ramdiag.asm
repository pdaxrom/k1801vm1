; Independent cold-boot diagnostic for the 18-bit TERAK memory profile.
; Resident code/stack use bank 0; all 30 other 8 KiB banks are filled first,
; then compared through the CPU MMU, including the former 160000 I/O hole.
; No extended-memory software or FPP is required. QB E5 softboot at address 0.
        org 0
        dw 10005
        br start
        dw 0
        org 40
start:  mov #340,@#177776
        mov #2000,sp
        mov #fail,@#4
        mov #340,@#6
        mov #1772340,r0
        clr r1
        mov #8.,r2
maps:   mov r1,(r0)+
        add #200,r1
        sob r2,maps
        mov #1772300,r0
        mov #8.,r2
pdrs:   mov #177406,(r0)+
        sob r2,pdrs
        mov #7600,@#1772356
        mov #1,@#177572
        clr r2
phase:  mov #1,r3
bank:   mov r3,r5
        asl r5
        asl r5
        asl r5
        asl r5
        asl r5
        asl r5
        asl r5
        mov r5,@#1772344
        mov #40000,r0
        mov #4096.,r1
        mov r3,r4
        swab r4
        tst r2
        bne verify
fill:   mov r4,(r0)+
        inc r4
        sob r1,fill
        br next
verify: cmp (r0)+,r4
        bne fail
        inc r4
        sob r1,verify
next:   inc r3
        cmp r3,#31.
        bne bank
        inc r2
        cmp r2,#2
        bne phase
        mov #good,r5
        br display
fail:   mov #bad,r5
display:clr @#177572
        mov #200,@#177744
        mov #fonts,r0
        mov #10.,r3
chars:  movb (r0)+,r1
        asl r1
        asl r1
        asl r1
        asl r1
        add #160000,r1
        mov #10.,r2
glyph:  movb (r0)+,(r1)+
        sob r2,glyph
        sob r3,chars
        clr @#177742
        mov #7,@#177744
        mov #162040,r0
text:   movb (r5)+,(r0)+
        bne text
done:   br done
good:   db "248K PASS",0
bad:    db "FAIL",0
; Original 8x10 diagnostic glyphs, bit 0 at the left edge.
fonts:  db 50.,0,0x3c,0x42,0x40,0x20,0x10,0x08,0x7e,0,0
        db 52.,0,0x20,0x30,0x28,0x24,0x22,0x7e,0x20,0,0
        db 56.,0,0x3c,0x42,0x42,0x3c,0x42,0x42,0x3c,0,0
        db 75.,0,0x42,0x22,0x12,0x0e,0x12,0x22,0x42,0,0
        db 80.,0,0x3e,0x42,0x42,0x3e,0x02,0x02,0x02,0,0
        db 65.,0,0x3c,0x42,0x42,0x7e,0x42,0x42,0x42,0,0
        db 83.,0,0x7c,0x02,0x02,0x3c,0x40,0x40,0x3e,0,0
        db 70.,0,0x7e,0x02,0x02,0x3e,0x02,0x02,0x02,0,0
        db 73.,0,0x3e,0x08,0x08,0x08,0x08,0x08,0x3e,0,0
        db 76.,0,0x02,0x02,0x02,0x02,0x02,0x02,0x7e,0,0
