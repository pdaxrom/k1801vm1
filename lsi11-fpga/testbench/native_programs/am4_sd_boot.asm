	cpu dcj-11
	org 0

; The hardware loader enters through its FRAM bootstrap at 02000.  Its final
; CLR PC must leave Z set and transfer here with the Stable boot ABI intact.
start
	bne failed
	tst r0
	bne failed
	cmp #0177440, r1
	bne failed
	tst r2
	bne failed
	tst r3
	bne failed
	cmp #002020, r4
	bne failed
	tst r5
	bne failed
	cmp #002000, sp
	bne failed
	jmp @#000100

; Occupy the retained ODT window exactly.  Reaching this marker proves the
; SD-success overlay was removed after vector 024/026.  The checks above also
; execute through 024/026 themselves.
	org 000100
	mov #012345, @#003000
passed
	br passed

	org 000200
failed
	mov #065432, @#003000
	br failed
