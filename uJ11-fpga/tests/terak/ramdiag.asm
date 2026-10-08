; Independent cold-boot diagnostic for the 18/22-bit TERAK memory profiles.
; Parameters are substituted by tools/test_terak_ramdiag.py.
; Resident code/stack use bank 0; all other 8 KiB banks are filled first,
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
        mov #@IO_PAR@,@#1772356
        mov #@MMR3@,@#172516
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
        cmp r3,#@BANKS@.
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
        mov #11.,r3
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
good:   db "@PASS_LABEL@",0
bad:    db "FAIL",0
; Same 8x10 VT52 glyphs and baseline as the BIOS. Standalone boot also
; installs the required characters; the surrounding BIOS text is unchanged.
fonts:
; @FONT@
