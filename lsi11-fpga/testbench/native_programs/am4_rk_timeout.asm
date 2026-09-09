	cpu dcj-11
	macro check
	cmp #1, #2
	beq *+6
	jmp @#failed
	endm
	org 0
	jmp @#start
	org 000400
start
	mov #disk_irq, @#000210
	clr @#000212
	clr @#014000
	mov #000040, @#0177450
	clr @#0177450
	clr @#0177446
	clr @#0177460
	mov #003000, @#0177444
	mov #012345, @#003000
	mov #-1, @#0177442
	mov #000123, @#0177440
wait_irq
	tst @#014000
	beq wait_irq
	check #1, @#014000
	check #0100322, @#0177440
	check #020000, @#0177454
	mov #012345, @#003200
passed
	br passed
failed
	mov #065432, @#003200
	br failed
disk_irq
	inc @#014000
	rti
