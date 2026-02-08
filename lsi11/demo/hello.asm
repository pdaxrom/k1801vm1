        org    2000

DL11_XBUF equ 0177566

start:
        mov     #msg, r1
loop:
        movb    (r1)+, r0
        beq     done
        movb    r0, @#DL11_XBUF
        br      loop
done:
        halt

msg:
        db  "Hello!\r\n", 0
