; Exactly representable F-format arithmetic plus all AM4 arithmetic-error
; classes.  Boundary rounding is intentionally outside this directed test.
	include am4_fis_test.inc
table
	; FADD 2 + 1 = 3
	dw do_add, 040200, 0, 040400, 0, 040500, 0, 0340, 0
	; FADD 1 + -1 = canonical zero
	dw do_add, 0140200, 0, 040200, 0, 0, 0, 0344, 0
	; FSUB 3 - 1 = 2
	dw do_sub, 040200, 0, 040500, 0, 040400, 0, 0340, 0
	; FSUB 1 - 2 = -1
	dw do_sub, 040400, 0, 040200, 0, 0140200, 0, 0350, 0
	; FMUL 2 * 2 = 4
	dw do_mul, 040400, 0, 040400, 0, 040600, 0, 0340, 0
	; FMUL -2 * 2 = -4
	dw do_mul, 0140400, 0, 040400, 0, 0140600, 0, 0350, 0
	; FDIV 4 / 2 = 2
	dw do_div, 040400, 0, 040600, 0, 040400, 0, 0340, 0
	; FDIV -4 / 2 = -2
	dw do_div, 040400, 0, 0140600, 0, 0140400, 0, 0350, 0
	; Zero exponent is canonicalized on success.
	dw do_add, 0100000, 0, 0, 0, 0, 0, 0344, 0
	; Overflow, underflow and divide-by-zero preserve A and do not advance R0.
	dw do_add, 077777, 0177777, 077777, 0177777, 077777, 0177777, 0342, 1
	dw do_mul, 0200, 0, 0200, 0, 0200, 0, 0352, 1
	dw do_div, 0, 0, 040200, 0, 040200, 0, 0353, 1
end_table
