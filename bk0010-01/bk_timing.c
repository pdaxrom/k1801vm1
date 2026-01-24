#include "bk_timing.h"

#define SIGN 0100000

#define SRC_MODE(op) (((op) & 07000) >> 9)
#define DST_MODE(op) (((op) & 00070) >> 3)

#define REGREG 12

static const unsigned a_time[8]  = {0, 12, 12, 20, 12, 20, 20, 28};
static const unsigned b_time[8]  = {0, 20, 20, 32, 20, 32, 32, 40};
static const unsigned ab_time[8] = {0, 16, 16, 24, 16, 24, 24, 32};
static const unsigned a2_time[8] = {0, 20, 20, 28, 20, 28, 28, 36};
static const unsigned ds_time[8] = {0, 32, 32, 40, 32, 40, 40, 48};

#define a1_time a_time
#define dj_time a2_time

#define dst_time(op) (SRC_MODE(op) ? ab_time : b_time)
#define cmp_time(op) (SRC_MODE(op) ? a1_time : a2_time)

enum inst
{
	illegal, adc, adcb, add, ash, ashc, asl, aslb, asr, asrb, bcc, bcs,
	beq, bge, bgt, bhi, bic, bicb, bis, bisb, bit, bitb, ble, blos, blt,
	bmi, bne, bpl, bpt, br, bvc, bvs, clr, clrb, ccc, cmp, cmpb, com,
	comb, dec, decb, divide, emt, halt, inc, incb, iot, jmp, jsr, mark,
	mfpd, mfpi, mfps, mov, movb, mtpd, mtpi, mtps, mul, neg, negb,
	busreset, rol, rolb, ror, rorb, rti, rts, rtt, sbc, sbcb, scc, sob,
	sub, swabi, sxt, trap, tst, tstb, waiti, xor, fis, itimtab0, itimtab1
};

static const enum inst sitimtab0[64] =
{
	halt, waiti, rti, bpt, iot, busreset, rtt, illegal,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal
};

static const enum inst sitimtab1[64] =
{
	rts, rts, rts, rts, rts, rts, rts, rts,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	ccc, ccc, ccc, ccc, ccc, ccc, ccc, ccc,
	ccc, ccc, ccc, ccc, ccc, ccc, ccc, ccc,
	scc, scc, scc, scc, scc, scc, scc, scc,
	scc, scc, scc, scc, scc, scc, scc, scc
};

static const enum inst itimtab[1024] =
{
	itimtab0, jmp, itimtab1, swabi, br, br, br, br,
	bne, bne, bne, bne, beq, beq, beq, beq,
	bge, bge, bge, bge, blt, blt, blt, blt,
	bgt, bgt, bgt, bgt, ble, ble, ble, ble,
	jsr, jsr, jsr, jsr, jsr, jsr, jsr, jsr,
	clr, com, inc, dec, neg, adc, sbc, tst,
	ror, rol, asr, asl, mark, illegal, illegal, sxt,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	mov, mov, mov, mov, mov, mov, mov, mov,
	mov, mov, mov, mov, mov, mov, mov, mov,
	mov, mov, mov, mov, mov, mov, mov, mov,
	mov, mov, mov, mov, mov, mov, mov, mov,
	mov, mov, mov, mov, mov, mov, mov, mov,
	mov, mov, mov, mov, mov, mov, mov, mov,
	mov, mov, mov, mov, mov, mov, mov, mov,
	mov, mov, mov, mov, mov, mov, mov, mov,
	cmp, cmp, cmp, cmp, cmp, cmp, cmp, cmp,
	cmp, cmp, cmp, cmp, cmp, cmp, cmp, cmp,
	cmp, cmp, cmp, cmp, cmp, cmp, cmp, cmp,
	cmp, cmp, cmp, cmp, cmp, cmp, cmp, cmp,
	cmp, cmp, cmp, cmp, cmp, cmp, cmp, cmp,
	cmp, cmp, cmp, cmp, cmp, cmp, cmp, cmp,
	cmp, cmp, cmp, cmp, cmp, cmp, cmp, cmp,
	cmp, cmp, cmp, cmp, cmp, cmp, cmp, cmp,
	bit, bit, bit, bit, bit, bit, bit, bit,
	bit, bit, bit, bit, bit, bit, bit, bit,
	bit, bit, bit, bit, bit, bit, bit, bit,
	bit, bit, bit, bit, bit, bit, bit, bit,
	bit, bit, bit, bit, bit, bit, bit, bit,
	bit, bit, bit, bit, bit, bit, bit, bit,
	bit, bit, bit, bit, bit, bit, bit, bit,
	bit, bit, bit, bit, bit, bit, bit, bit,
	bic, bic, bic, bic, bic, bic, bic, bic,
	bic, bic, bic, bic, bic, bic, bic, bic,
	bic, bic, bic, bic, bic, bic, bic, bic,
	bic, bic, bic, bic, bic, bic, bic, bic,
	bic, bic, bic, bic, bic, bic, bic, bic,
	bic, bic, bic, bic, bic, bic, bic, bic,
	bic, bic, bic, bic, bic, bic, bic, bic,
	bic, bic, bic, bic, bic, bic, bic, bic,
	bis, bis, bis, bis, bis, bis, bis, bis,
	bis, bis, bis, bis, bis, bis, bis, bis,
	bis, bis, bis, bis, bis, bis, bis, bis,
	bis, bis, bis, bis, bis, bis, bis, bis,
	bis, bis, bis, bis, bis, bis, bis, bis,
	bis, bis, bis, bis, bis, bis, bis, bis,
	bis, bis, bis, bis, bis, bis, bis, bis,
	bis, bis, bis, bis, bis, bis, bis, bis,
	add, add, add, add, add, add, add, add,
	add, add, add, add, add, add, add, add,
	add, add, add, add, add, add, add, add,
	add, add, add, add, add, add, add, add,
	add, add, add, add, add, add, add, add,
	add, add, add, add, add, add, add, add,
	add, add, add, add, add, add, add, add,
	add, add, add, add, add, add, add, add,
	mul, mul, mul, mul, mul, mul, mul, mul,
	divide, divide, divide, divide, divide, divide, divide, divide,
	ash, ash, ash, ash, ash, ash, ash, ash,
	ashc, ashc, ashc, ashc, ashc, ashc, ashc, ashc,
	xor, xor, xor, xor, xor, xor, xor, xor,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	sob, sob, sob, sob, sob, sob, sob, sob,
	bpl, bpl, bpl, bpl, bmi, bmi, bmi, bmi,
	bhi, bhi, bhi, bhi, blos, blos, blos, blos,
	bvc, bvc, bvc, bvc, bvs, bvs, bvs, bvs,
	bcc, bcc, bcc, bcc, bcs, bcs, bcs, bcs,
	emt, emt, emt, emt, trap, trap, trap, trap,
	clrb, comb, incb, decb, negb, adcb, sbcb, tstb,
	rorb, rolb, asrb, aslb, mtps, illegal, illegal, mfps,
	illegal, illegal, illegal, illegal, illegal, illegal, illegal, illegal,
	movb, movb, movb, movb, movb, movb, movb, movb,
	movb, movb, movb, movb, movb, movb, movb, movb,
	movb, movb, movb, movb, movb, movb, movb, movb,
	movb, movb, movb, movb, movb, movb, movb, movb,
	movb, movb, movb, movb, movb, movb, movb, movb,
	movb, movb, movb, movb, movb, movb, movb, movb,
	movb, movb, movb, movb, movb, movb, movb, movb,
	movb, movb, movb, movb, movb, movb, movb, movb,
	cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb,
	cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb,
	cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb,
	cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb,
	cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb,
	cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb,
	cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb,
	cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb, cmpb,
	bitb, bitb, bitb, bitb, bitb, bitb, bitb, bitb,
	bitb, bitb, bitb, bitb, bitb, bitb, bitb, bitb,
	bitb, bitb, bitb, bitb, bitb, bitb, bitb, bitb,
	bitb, bitb, bitb, bitb, bitb, bitb, bitb, bitb,
	bitb, bitb, bitb, bitb, bitb, bitb, bitb, bitb,
	bitb, bitb, bitb, bitb, bitb, bitb, bitb, bitb,
	bitb, bitb, bitb, bitb, bitb, bitb, bitb, bitb,
	bitb, bitb, bitb, bitb, bitb, bitb, bitb, bitb,
	bicb, bicb, bicb, bicb, bicb, bicb, bicb, bicb,
	bicb, bicb, bicb, bicb, bicb, bicb, bicb, bicb,
	bicb, bicb, bicb, bicb, bicb, bicb, bicb, bicb,
	bicb, bicb, bicb, bicb, bicb, bicb, bicb, bicb,
	bicb, bicb, bicb, bicb, bicb, bicb, bicb, bicb,
	bicb, bicb, bicb, bicb, bicb, bicb, bicb, bicb,
	bicb, bicb, bicb, bicb, bicb, bicb, bicb, bicb,
	bicb, bicb, bicb, bicb, bicb, bicb, bicb, bicb,
	bisb, bisb, bisb, bisb, bisb, bisb, bisb, bisb,
	bisb, bisb, bisb, bisb, bisb, bisb, bisb, bisb,
	bisb, bisb, bisb, bisb, bisb, bisb, bisb, bisb,
	bisb, bisb, bisb, bisb, bisb, bisb, bisb, bisb,
	bisb, bisb, bisb, bisb, bisb, bisb, bisb, bisb,
	bisb, bisb, bisb, bisb, bisb, bisb, bisb, bisb,
	bisb, bisb, bisb, bisb, bisb, bisb, bisb, bisb,
	bisb, bisb, bisb, bisb, bisb, bisb, bisb, bisb,
	sub, sub, sub, sub, sub, sub, sub, sub,
	sub, sub, sub, sub, sub, sub, sub, sub,
	sub, sub, sub, sub, sub, sub, sub, sub,
	sub, sub, sub, sub, sub, sub, sub, sub,
	sub, sub, sub, sub, sub, sub, sub, sub,
	sub, sub, sub, sub, sub, sub, sub, sub,
	sub, sub, sub, sub, sub, sub, sub, sub,
	sub, sub, sub, sub, sub, sub, sub, sub,
	fis, fis, fis, fis, fis, fis, fis, fis,
	fis, fis, fis, fis, fis, fis, fis, fis,
	fis, fis, fis, fis, fis, fis, fis, fis,
	fis, fis, fis, fis, fis, fis, fis, fis,
	fis, fis, fis, fis, fis, fis, fis, fis,
	fis, fis, fis, fis, fis, fis, fis, fis,
	fis, fis, fis, fis, fis, fis, fis, fis,
	fis, fis, fis, fis, fis, fis, fis, fis
};

unsigned bk_timing_cycles(word op)
{
	enum inst code = itimtab[op >> 6];
	switch (code) {
	case itimtab0:
		code = sitimtab0[op & 077];
		break;
	case itimtab1:
		code = sitimtab1[op & 077];
		break;
	default:
		break;
	}

	switch (code) {
	case mov:
	case movb:
	case bic:
	case bicb:
	case bis:
	case bisb:
	case add:
	case sub:
		return REGREG + a_time[SRC_MODE(op)] + dst_time(op)[DST_MODE(op)];
	case cmp:
	case cmpb:
	case bit:
	case bitb:
		return REGREG + a1_time[SRC_MODE(op)] + cmp_time(op)[DST_MODE(op)];
	case clr:
	case clrb:
	case com:
	case comb:
	case neg:
	case negb:
	case inc:
	case incb:
	case dec:
	case decb:
	case adc:
	case adcb:
	case sbc:
	case sbcb:
	case ror:
	case rorb:
	case rol:
	case rolb:
	case asl:
	case aslb:
	case asr:
	case asrb:
	case mtps:
	case mfps:
	case swabi:
	case sxt:
		return REGREG + ab_time[DST_MODE(op)];
	case tst:
	case tstb:
		return REGREG + a1_time[DST_MODE(op)];
	case bne:
	case beq:
	case bge:
	case blt:
	case bgt:
	case ble:
	case bpl:
	case bmi:
	case bhi:
	case blos:
	case bvc:
	case bvs:
	case bcc:
	case bcs:
	case br:
		return 16;
	case jmp:
		return dj_time[DST_MODE(op)];
	case jsr:
		return ds_time[DST_MODE(op)];
	case sob:
		return 20;
	case rts:
		return 32;
	case scc:
	case ccc:
		return REGREG;
	case rti:
	case rtt:
		return 40;
	case trap:
	case emt:
	case bpt:
	case iot:
		return 68;
	case xor:
		return REGREG + a2_time[DST_MODE(op)];
	case mark:
		return 36;
	case illegal:
	case halt:
	case fis:
	case mul:
	case divide:
	case ash:
	case ashc:
		return 144;
	case waiti:
	case busreset:
		return 1167;
	default:
		break;
	}
	return 0;
}
