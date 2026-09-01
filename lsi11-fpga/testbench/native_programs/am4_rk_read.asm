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
	check #0x0A03, @#003000
	check #0x1811, @#003002
	check #0x261F, @#003004

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

	; Q-bus DMA uses physical addresses.  Exercise two sectors hidden under the
	; CPU I/O page so the transfer crosses from 160000 into 161000.  The READ
	; must write physical FRAM rather than the service ROM, and the following
	; WRITE must read both sectors back without treating 160476 as service code.
	mov #0160000, @#rkba
	mov #000025, @#rkda
	mov #-001000, @#rkwc
	mov #000121, r3
	bis r2, r3
	mov r3, @#rkcs1
wait_high_read_irq
	cmp #3, @#014000
	bne wait_high_read_irq
	check #002320, @#rkcs1
	check #0, @#rkwc
	check #0162000, @#rkba
	check #000401, @#rkda

	mov #0160000, @#rkba
	mov #000010, @#rkda
	clr @#rkdc
	mov #-001000, @#rkwc
	mov #000123, r3
	bis r2, r3
	mov r3, @#rkcs1
wait_high_write_irq
	cmp #4, @#014000
	bne wait_high_write_irq
	check #002322, @#rkcs1
	check #0, @#rkwc
	check #0162000, @#rkba
	check #000012, @#rkda

	; Exercise the actual device-CSR end of the physical I/O page.  READ one
	; sector through 177000..177777, including the RK and KL11 addresses.  Then
	; issue a one-word WRITE.  The SD backend still consumes a complete sector,
	; but RK WC/BA must stop after one word and the remaining words are zero-fill.
	mov #0177000, @#rkba
	mov #000401, @#rkda
	mov #-000400, @#rkwc
	mov #000121, r3
	bis r2, r3
	mov r3, @#rkcs1
wait_io_page_read_irq
	cmp #5, @#014000
	bne wait_io_page_read_irq
	check #002320, @#rkcs1
	check #0, @#rkwc
	check #0, @#rkba
	check #000402, @#rkda

	mov #0177000, @#rkba
	mov #000012, @#rkda
	clr @#rkdc
	mov #-1, @#rkwc
	mov #000123, r3
	bis r2, r3
	mov r3, @#rkcs1
	wait_partial_write_irq
	cmp #6, @#014000
	bne wait_partial_write_irq
	check #002322, @#rkcs1
	check #0, @#rkwc
	check #0177002, @#rkba
	check #000013, @#rkda
	mov #012345, @#003200
passed
	br passed

failed
	mov #065432, @#003200
	br failed

disk_irq
	inc @#014000
	rti
