; ABI2 HALT helper, uploaded by UJLOAD into FRAM, never into FPGA ROM.
; R5 -> USER descriptor: operation, raw upper address, value.
; Operations: 0 read, 1 write/readback, 2 ready mask, 3 status, 4 initialize.
; Result R0=data/status, R2=0 success or 1 failure. R1/R3/R5 clobbered.
; Caller enters via resident vector 170 with R4=125061, R2=0, R1=1000.
	cpu dcj-11
	org 001000
loader_entry
	mov @#000004, r4
	mov #failed, @#000004
	dw 000021
	mov r0, r3
	dw 000021
	mov r0, r1
	dw 000021
	cmp r3, #4
	bhi block
	cmp r3, #4
	beq initialize
	cmp r3, #3
	beq status
	cmp r3, #2
	beq configure
	cmp r3, #1
	bhi failed
	bit #1, r1
	bne failed
	mov r1, r5
	tst r3
	beq read
	cmp r5, #001000
	bcs failed
	mov r0, r3
	dw 000041               ; HALT-only raw upper write, including I/O backing
	dw 000040
	cmp r0, r3
	bne failed
	br success
read
	cmp r5, #4
	bne physical_read
	mov r4, r0
	br success
physical_read
	dw 000040
	br success
configure
	cmp r0, #3
	bhi failed
	dw 000042
	br select_odt
status
	dw 000043
	br success
initialize
; An active module's system records are checked by UJLOAD before replacing it.
; Do not silently repair them here. No ready modules: install the common ABI.
	dw 000043
	bit #3, r0
	bne identify
	mov #000177, @#000320   ; JMP @000164, uses no guest register or guest stack
	mov #0177640, @#000322  ; 164 - 324
	mov #000777, @#000326   ; No loaded ODT: remain in HALT
	mov #module_return, @#000166
	mov #002020, @#000010
	mov #000340, @#000012
select_odt
	mov #000326, @#000164
	bit #1, r0
	beq identify
	mov #002000, @#000164
identify
	mov #0125062, r0
success
	clr r2
leave
	mov #000312, @#000004   ; Resident copy/execute failures use its own START
	dw 000010
failed
	mov #1, r2
	br leave
; Bounded block calls. value=count (1..8 words). Copy op7 adds a fourth
; descriptor word, USER source. The 65536-byte raw endpoint is legal.
block
	cmp r3, #000010
	bhi failed
	tst r0
	beq failed
	cmp r0, #000010
	bhi failed
	bit #1, r1
	bne failed
	cmp r1, #001000
	bcs failed
	mov r0, r2
	asl r0
	add r1, r0
	bcc block_range
	tst r0
	bne failed
block_range
	cmp r3, #7
	bne block_ready
	dw 000021
	bit #1, r0
	bne failed
	mov r0, r4
	add r2, r0
	bcs failed
	add r2, r0
	bcs failed
	cmp r0, #0160000
	bhi failed
block_ready
	mov r1, r5
	clr r1
block_loop
	cmp r3, #000010
	beq block_check_zero
	cmp r3, #5
	beq block_read
	cmp r3, #6
	beq block_zero
	mov r5, r1
	mov r4, r5
	dw 000021
	mov r5, r4
	mov r1, r5
	br block_write
block_zero
	clr r0
block_write
	mov r0, r1
	dw 000041
	dw 000040
	cmp r0, r1
	bne failed
	br block_next
block_check_zero
	dw 000040
	tst r0
	bne failed
	br block_next
block_read
	dw 000040
	add r0, r1
block_next
	add #2, r5
	sob r2, block_loop
	mov r1, r0
	jmp @#success
	org 001700
module_return
; ABI2 module exit: JMP @000166. Preserve R0-R6 and restore original CPSW.
	mov #000312, @#000004
	dw 000010
loader_end
