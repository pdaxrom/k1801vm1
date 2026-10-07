; uJ11 RLV11 bootstrap, using the DEC documented register protocol.
; Execute from ROM so reading disk block zero cannot overwrite this loader.
        org 173000
start:  mtps #340
        mov #150000,sp
        mov #174400,r1
        mov #13,4(r1)           ; Get Status with drive error reset
        mov #4,(r1)
        jsr pc,wait
        mov #10,(r1)           ; Read Header: current cylinder/head
        jsr pc,wait
        mov 6(r1),r2
        bic #177,r2            ; distance back to cylinder zero
        bis #1,r2              ; seek inward, head zero
        mov r2,4(r1)
        mov #6,(r1)
        jsr pc,wait
        clr 2(r1)
        clr 4(r1)
        mov #-256.,6(r1)       ; first 512-byte RT-11 bootstrap block
        mov #14,(r1)
        jsr pc,wait
        clr r0                 ; unit zero, standard DEC boot convention
        jmp @#0
wait:   tstb (r1)
        bpl wait
        tst (r1)
        bmi fail
        rts pc
fail:   halt
        br fail
