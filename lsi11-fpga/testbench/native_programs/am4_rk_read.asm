	cpu dcj-11
	macro check
	cmp #1, #2
	beq *+6
	jmp @#failed
	endm
	org 0
	jmp @#start

rkcs1 equ 0177440
rkwc equ 0177442
rkba equ 0177444
rkda equ 0177446
rkcs2 equ 0177450
rkds equ 0177452
rkdc equ 0177460

	org 000400
start
	beq boot_flags_ok
	jmp @#failed
boot_flags_ok
	check #0177440, r1
	check #002000, sp
	; Follow the real RT-11 sector-zero bootstrap through controller clear,
	; drive-type discovery and PACK ACK instead of jumping straight to READ.
	mov #000040, @#rkcs2
	clr @#rkcs2
wait_drive
	mov @#rkds, r2
	bpl wait_drive
	bic #0177377, r2
	asl r2
	asl r2
	mov #3, r3
	bis r2, r3
	mov r3, @#rkcs1
wait_pack
	tstb @#rkcs1
	bpl wait_pack
	mov #disk_irq, @#000210
	mov #000340, @#000212
	clr @#014000
	mov #003000, @#rkba
	mov #000025, @#rkda
	mov #-3, @#rkwc
	mov #000121, r3
	bis r2, r3
	mov r3, @#rkcs1

wait_irq
	tst @#014000
	beq wait_irq
	check #002320, @#rkcs1
	check #0, @#rkwc
	check #003006, @#rkba
	check #000400, @#rkda
	check #0, @#rkdc
	check #0xB05A, @#003000
	check #0xB05B, @#003002
	check #0xB058, @#003004

	; Fill one complete guest block and issue RK WRITE to LBA 7.  The private
	; service must send CMD24, update WC/BA/DA, and deliver a second vector.
	mov #004000, r4
	mov #000400, r0
	mov #012345, r3
fill_write_block
	mov r3, (r4)+
	inc r3
	sob r0, fill_write_block
	mov #004000, @#rkba
	mov #000007, @#rkda
	clr @#rkdc
	mov #-000400, @#rkwc
	mov #000123, r3
	bis r2, r3
	mov r3, @#rkcs1
wait_write_irq
	cmp #2, @#014000
	bne wait_write_irq
	check #002322, @#rkcs1
	check #0, @#rkwc
	check #005000, @#rkba
	check #000010, @#rkda
	check #0, @#rkdc
	mov #012345, @#003200
passed
	br passed

failed
	mov #065432, @#003200
	br failed

disk_irq
	inc @#014000
	rti
