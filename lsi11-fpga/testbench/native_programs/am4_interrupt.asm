	cpu dcj-11
	org 0
	dw 0

	org 024
	dw start
	dw 0

	org 060
	dw handler
	dw 0

	org 0100
start
	mov #04000, sp
	clr @#0200
idle
	br idle

handler
	inc @#0200
	rti
