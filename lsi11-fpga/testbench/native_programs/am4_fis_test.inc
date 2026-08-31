; Shared AM4 FIS guest harness.  The including source supplies table and
; end_table.  After the routine word, each row is B-hi, B-lo, A-hi, A-lo,
; result-hi, result-lo, expected PSW low byte, and expected exception (0/1).
	cpu dcj-11
	macro check
	cmp #1, #2
	beq *+6
	jmp @#failed
	endm
	org 0
	dw failed
	dw 0340
	org 4
	dw failed
	dw 0340
	org 010
	dw failed
	dw 0340
	org 024
	dw start
	dw 0
	org 0244
	dw arithmetic_error
	dw 0340
	done equ 06000
	fail_case equ 06002
	aux equ 06004
	operands equ 04000
	trapped equ 04010
	trap_resume equ 04012
	org 01000
start
	mov #010000, sp
	mov #table, r1
	clr r5
next_case
	inc r5
	mov (r1)+, r2
	mov #operands, r0
	mov (r1), (r0)
	mov 2(r1), 2(r0)
	mov 4(r1), 4(r0)
	mov 6(r1), 6(r0)
	clr @#trapped
	mtps #0357
	jsr r4, (r2)
	bic #0177400, r3
	check 014(r1), r3
	check 016(r1), @#trapped
	check (r1), @#operands
	check 2(r1), @#operands+2
	check 010(r1), @#operands+4
	check 012(r1), @#operands+6
	tst @#trapped
	bne error_pointer
	check #operands+4, r0
	br checked_pointer
error_pointer
	check #operands, r0
checked_pointer
	check #010000, sp
	add #020, r1
	cmp #end_table, r1
	bne next_case
	mov #1, @#done
passed
	br passed
do_add
	mov #add_return, @#trap_resume
	fadd r0
add_return
	mfps r3
	rts r4
do_sub
	mov #sub_return, @#trap_resume
	fsub r0
sub_return
	mfps r3
	rts r4
do_mul
	mov #mul_return, @#trap_resume
	fmul r0
mul_return
	mfps r3
	rts r4
do_div
	mov #div_return, @#trap_resume
	fdiv r0
div_return
	mfps r3
	rts r4
arithmetic_error
	check @#trap_resume, (sp)
	check 014(r1), 2(sp)
	inc @#trapped
	rti
failed
	mov r5, @#fail_case
	mov r1, @#aux
	mov r3, @#aux+2
	mov r0, @#aux+4
	mov sp, @#aux+6
	mov #0177777, @#done
	br failed
	org 010000
