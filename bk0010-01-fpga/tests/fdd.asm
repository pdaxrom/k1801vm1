        .org    0400
start:
        mov     #01000,sp
        mov     #02000,r3
        jsr     pc,@#0160010
        clr     r0
        mov     #0400,r1
        mov     #04000,r2
        jsr     pc,@#0160004
        bcs     failed
        mov     #01111,@#03000
        mov     #04000,r2
        mov     #0400,r1
fill:
        mov     #042102,(r2)+
        sob     r1,fill
        clr     r0
        mov     #-0400,r1
        mov     #04000,r2
        jsr     pc,@#0160004
        bcs     failed
        mov     #02222,@#03000
        movb    #1,@#02034
        clr     r0
        mov     #-0400,r1
        mov     #04000,r2
        jsr     pc,@#0160004
        bcc     failed
        mov     #03333,@#03000
        clrb    @#02034
        clrb    @#02022
        clrb    @#02032
        clrb    @#02033
        movb    #0102,@#02017
        mov     #0400,@#02064
        jsr     pc,@#0160012
        bcs     failed
        mov     #04444,@#03000
stop:
        br      stop
failed:
        mov     #0177777,@#03000
        br      stop
