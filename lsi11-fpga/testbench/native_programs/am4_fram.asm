	cpu dcj-11
	org 0
	dw 0

	org 024
	dw start
	dw 0

	org 0100
start
	mov #04000, sp
	mov #012345, @#0200
	inc @#0200
	movb #0123, @#0202
	movb #0256, @#0203
	mov #1, @#0204
passed
	br passed
