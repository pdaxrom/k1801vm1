	cpu dcj-11
	org 0
	dw 0

	org 024
	dw start
	dw 0

	org 0100
start
	mov #012345, r0
	mov r0, @#0200
	cmp #012345, @#0200
	bne failed
	mov #1, @#0202
passed
	br passed

failed
	mov #0177777, @#0202
	br failed
