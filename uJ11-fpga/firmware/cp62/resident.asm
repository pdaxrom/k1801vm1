; CP62 cold-start gate. This PDP-11 program is copied from ROM into HALT RAM.
; Calling convention: R4=125061, R5=USER source, R1=HALT destination,
; R2=1..64 words. Clobbers R0/R1/R2/R3/R5; preserves R4/SP and saved PSW.
; R0 result: 0 success, 1 bad range, bus error or readback mismatch.
; Whole ranges must be even, below the shared I/O page, destination >=001000.
; No RT-11 calls, no stack, no UJRD/UJWR and no implicit module enable.
	cpu dcj-11
	org 000200
resident_entry
	cmp r4, #0125061
	bne idle
	cmp r2, #000100
	bhi invalid
	mov r5, r0
	bis r1, r0
	bit #1, r0
	bne invalid
	cmp r1, #001000
	bcs invalid
	mov r2, r3
	asl r3
	add r5, r3
	bcs invalid
	cmp r3, #0160000
	bhi invalid
	mov r2, r3
	asl r3
	add r1, r3
	bcs invalid
	cmp r3, #0160000
	bhi invalid
	tst r2
	beq execute
copy
	dw 000021               ; MFUS: R0=USER[R5], R5+=2
	mov r0, (r1)
	cmp r0, (r1)+
	bne fault
	sob r2, copy
	clr r0
return
	dw 000010               ; START: restore guest CPC/CPSW and USER bank
fault
invalid
	mov #1, r0
	br return
idle
	br idle                 ; Full ODT is a separately loaded future module.
initialize
; Executed only after the ROM has copied AND read back this entire payload.
	mov #000200, @#000170
	mov #000340, r0
	mov r0, @#000172
	mov r0, @#000006
	mov #idle, @#000004     ; Cold-copy faults must not start incomplete USER code.
	mov #001000, sp
	mov #copy_user, @#000160 ; HALT subroutine vector, one address word
	mov #004652, r1         ; End of bootstrap copy in HALT RAM
	mov r1, r5              ; End of destination in USER RAM
	mov #000325, r2         ; 213 words, original bootstrap is exactly 426 bytes
	jsr pc, @000160
	mov #fault, @#000004
	mov r1, @#000100        ; CPC: copied start address 004000
	clr @#000102            ; CPSW
	dw 000010               ; START, not RTI
copy_user
; Trusted HALT caller supplies even end pointers and a nonzero word count.
; MTUS writes backward; MFUS reads back the same USER word and advances R5.
	mov -(r1), r0
	dw 000031               ; MTUS
	dw 000021               ; MFUS
	cmp r0, (r1)
	bne idle
	tst -(r5)              ; Same decrement; harmless HALT bootstrap read saves one ROM word
	sob r2, copy_user
	rts pc
execute
	jmp (r1)               ; R2=0: explicit call of an uploaded HALT routine
resident_end
