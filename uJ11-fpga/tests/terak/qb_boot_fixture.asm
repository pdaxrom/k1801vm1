; Cold QB DMA bootstrap must load all three following sectors before this runs.
        org 0
        dw 10005                 ; QB DSDD media identifier; rewritten by ROM.
        br start
        dw 0
        org 40                   ; Keep BPT vector 14/16 outside executable code.
start:  mov (sp),@#14            ; Sector 1 replaced the trap vectors in RAM.
        mov #340,@#16
        tst r1
        bne fail
        cmp r2,#1000
        bne fail
        cmp @#1000,#(63.*256.+62.) ; track 1 sector 3 pattern
        bne fail
        cmp @#2000,#(97.*256.+96.) ; track 1 sector 5 pattern
        bne fail
        cmp @#3000,#(131.*256.+130.) ; track 1 sector 7 pattern
        bne fail
        mov @#171000,r4          ; Original QB.SYS RQM/DIO word-poll sequence.
        bpl fail
        rol r4
        bmi fail
        mov #3,r0
        mov #1,r1                ; one-word partial sector through BPT handler
        mov #20000,r2
        mov #401,r3              ; head 1, sector 1
        bpt
        cmp r0,#3
        bne fail
        tst r1
        bne fail
        cmp r2,#20002
        bne fail
        cmp @#20000,#(88.*256.+87.)
        bne fail
        mov #125252,@#100000
passed: br passed
fail:   mov #052525,@#100000
        br fail
