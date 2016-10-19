/*
 * core.c
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#include <stdio.h>
#include "core.h"

#define TYPE_WORD	0
#define TYPE_BYTE	1

#define TYPE_REG	0
#define TYPE_MEM	1
#define TYPE_ERROR	2

#define	MPI	0077777		/* most positive integer */
#define MNI	0100000		/* most negative integer */
#define NEG_1	0177777		/* negative one */
#define SIGN	0100000		/* sign bit */
#define CARRY   0200000		/* set if carry out */

#define	MPI_B	0177		/* most positive integer (byte) */
#define MNI_B	0200		/* most negative integer (byte) */
#define NEG_1_B	0377		/* negative one (byte) */
#define SIGN_B	0200		/* sign bit (byte) */
#define CARRY_B	0400		/* set if carry out (byte) */

#define unimpl() {	\
    fprintf(stderr, "Unimplemented %o at %o\n", op, r->r[7]); \
    break;		\
}

#define clear_flag(f) {	\
    r->psw &= ~(f);	\
}

#define set_flag(f) {	\
    r->psw |= (f);	\
}

#define flag_is_set(f)   ((r->psw & (f)) == (f))
#define flag_is_clear(f) ((r->psw & (f)) ==  0 )

#define set_flag_if(a, b) {	\
    if (a) {				\
    	set_flag(b);		\
    } else {				\
    	clear_flag(b);		\
    }						\
}

#define pushw(v) {			\
	r->r[6] -= 2;			\
	store_word(r, r->r[6], v); \
}

#define pullw(v) {			\
	v = load_word(r, r->r[6]); \
	r->r[6] += 2;			\
}

void core_reset(regs *r)
{
    r->r[7] = r->SEL1 & 0177400;
    r->psw = 0340;
    r->fWait = 0;
    r->fTrap = 0;
}

static INLINE byte load_byte(regs *r, word offset)
{
    return r->mem[offset];
}

static INLINE void store_byte(regs *r, word offset, byte value) {
	r->mem[offset] = value;
}

static INLINE word load_word(regs *r, word offset)
{
    return load_byte(r, offset) | (load_byte(r, offset + 1) << 8);
}

static INLINE void store_word(regs *r, word offset, word value) {
	store_byte(r, offset,     value & 0377);
	store_byte(r, offset + 1, value >> 8);
}

static INLINE byte get_data_byte(regs *r, byte type, word offset) {
	if (type == TYPE_REG) {
		return r->r[offset] & 0377;
	} else {
		return load_byte(r, offset);
	}
}

static INLINE void put_data_byte(regs *r, byte type, word offset, byte value) {
	if (type == TYPE_REG) {
		r->r[offset] = (r->r[offset] & 0177400) | value;
	} else {
		store_byte(r, offset, value);
	}
}

static INLINE word get_data_word(regs *r, byte type, word offset) {
	if (type == TYPE_REG) {
		return r->r[offset];
	} else {
		return load_word(r, offset);
	}
}

static INLINE void put_data_word(regs *r, byte type, word offset, word value) {
	if (type == TYPE_REG) {
		r->r[offset] = value;
	} else {
		store_word(r, offset, value);
	}
}

static INLINE byte decode_data(regs *r, byte data, byte data_type, word *offset)
{
    byte reg = data & 0007;
    byte mode = (data & 0070) >> 3;
    byte step;


    if (data_type == TYPE_WORD) {
    	step = 2;
    } else {
    	step = 1;
    }

    // modes #n, @#A, A or X(PC), @A or @X(PC)
    // increase PC to 2
    if (data == 0027 || data == 0037 || data == 0067 || data == 77) {
    	step = 2;
    }

    // The increment/decrement is always 2 bytes for modes 3 and 5,
    // or if the register being used is R6 (the stack pointer SP).
    if (reg == 6 || mode == 3 || mode == 5) {
    	step = 2;
    }

    switch (mode) {
    case 0: /* Rn */
    	*offset = reg;
    	return TYPE_REG;
    case 1: /* (Rn) */
    	*offset = r->r[reg];
    	return TYPE_MEM;
    case 2: /* (Rn)+ */
    	*offset = r->r[reg];
    	r->r[reg] += step;
    	return TYPE_MEM;
    case 3: /* @(Rn)+ */
    	*offset = load_word(r, r->r[reg]);
    	r->r[reg] += step;
    	return TYPE_MEM;
    case 4: /* -(Rn) */
    	r->r[reg] -= step;
    	*offset = r->r[reg];
    	return TYPE_MEM;
    case 5: /* @-(Rn) */
    	r->r[reg] -= step;
    	*offset = load_word(r, r->r[reg]);
    	return TYPE_MEM;
    case 6: /* X(Rn) */ {
    	word tmp = load_word(r, r->r[7]);
    	r->r[7] += 2;
    	*offset = r->r[reg] + tmp;
    	}
    	return TYPE_MEM;
    case 7: /* @X(Rn) */ {
    	word tmp = load_word(r, r->r[7]);
    	r->r[7] += 2;
    	*offset = load_word(r, r->r[reg] + tmp);
    	}
    	return TYPE_MEM;
    }

    // Never return here.
    return TYPE_ERROR;
}

int core_step(regs *r)
{
    if (r->fWait) {
    	return 0;
    }

    if (flag_is_set(FLAG_T)) {
    	if (!r->fTrap) {
    		pushw(r->psw);
    		pushw(r->r[7]);
    		r->r[7] = load_word(r, 14);
    		r->psw  = load_word(r, 16);
    	}
    }

    if (r->fTrap) {
    	r->fTrap = 0;
    }

    // load instruction

    word op = load_word(r, r->r[7]);
    r->r[7] += 2;

    //
    // No operands instructions
    //
    switch(op) {
    case 000000: /* HALT */
#warning HALT
    	r->r[7] = (r->SEL1 & 0177400) + 4;
    	return 0;

    case 000001: /* WAIT */
    	r->fWait = 1;
    	return 0;

    case 000002: /* RTI */
    	pullw(r->r[7]);
    	pullw(r->psw);
    	return 0;

    case 000003: /* BPT */
    	pushw(r->psw);
    	pushw(r->r[7]);
    	r->r[7] = load_word(r, 14);
    	r->psw  = load_word(r, 16);
    	return 0;

    case 000004: /* IOT */
    	pushw(r->psw);
    	pushw(r->r[7]);
    	r->r[7] = load_word(r, 20);
    	r->psw  = load_word(r, 22);
    	return 0;

    case 000005: /* RESET */
#warning RESET
    	return 0;

    case 000006: /* RTT */
    	pullw(r->r[7]);
    	pullw(r->psw);
    	r->fTrap = 1;
    	return 0;

    case 000240: /* NOP */
    	return 0;

    case 000241: /* CLC */
    	clear_flag(FLAG_C);
    	return 0;

    case 000242: /* CLV */
    	clear_flag(FLAG_V);
    	return 0;

    case 000244: /* CLZ */
    	clear_flag(FLAG_Z);
    	return 0;

    case 000250: /* CLN */
    	clear_flag(FLAG_N);
    	return 0;

    case 000257: /* CCC */
    	clear_flag(FLAG_C | FLAG_V | FLAG_Z | FLAG_N);
    	return 0;

    case 000261: /* SEC */
    	set_flag(FLAG_C);
    	return 0;

    case 000262: /* SEV */
    	set_flag(FLAG_V);
    	return 0;

    case 000264: /* SEZ */
    	set_flag(FLAG_Z);
    	return 0;

    case 000270: /* SEN */
    	set_flag(FLAG_N);
    	return 0;

    case 000277: /* SCC */
    	set_flag(FLAG_C | FLAG_V | FLAG_Z | FLAG_N);
    	return 0;

    }

    word offset = op & 0377;
    if (offset & SIGN_B) {
    	offset += 0177400;
    }

    switch (op & 0177400) {
    case 0000400: /* BR */
    	r->r[7] += (offset * 2);
    	return 0;

    case 0001000: /* BNE */
    	if (flag_is_clear(FLAG_Z)) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0001400: /* BEQ */
    	if (flag_is_set(FLAG_Z)) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0100000: /* BPL */
    	if (flag_is_clear(FLAG_N)) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0100400: /* BMI */
    	if (flag_is_set(FLAG_N)) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0102000: /* BVC */
    	if (flag_is_clear(FLAG_V)) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0102400: /* BVS */
    	if (flag_is_set(FLAG_V)) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0103000: /* BCC or BHIS */
    	if (flag_is_clear(FLAG_C)) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0103400: /* BCS or BLO */
    	if (flag_is_set(FLAG_C)) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0002000: /* BGE */
    	if ((flag_is_set(FLAG_N) ^ flag_is_set(FLAG_V)) == 0) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0002400: /* BLT */
    	if ((flag_is_set(FLAG_N) ^ flag_is_set(FLAG_V)) == 1) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0003000: /* BGT */
    	if (((flag_is_set(FLAG_N) ^ flag_is_set(FLAG_V)) == 0) && flag_is_clear(FLAG_Z)) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0003400: /* BLE */
    	if (((flag_is_set(FLAG_N) ^ flag_is_set(FLAG_V)) == 1) && flag_is_set(FLAG_Z)) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0101000: /* BHI */
    	if (flag_is_clear(FLAG_C | FLAG_Z)) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0101400: /* BLOS */
    	if (!flag_is_clear(FLAG_C | FLAG_Z)) {
    		r->r[7] += (offset * 2);
    	}
    	return 0;

    case 0104000:
    	if (op & 0400) {
    		// TRAP
        	pushw(r->psw);
        	pushw(r->r[7]);
        	r->r[7] = load_word(r, 34);
        	r->psw  = load_word(r, 36);
    	} else {
    		// EMT
        	pushw(r->psw);
        	pushw(r->r[7]);
        	r->r[7] = load_word(r, 30);
        	r->psw  = load_word(r, 32);
    	}
    	return 0;
    }

    // RTS
    if ((op & 0177770) == 000200) {
    	byte reg = op & 07;
    	r->r[7] = r->r[reg];
    	pullw(r->r[reg]);
    	return 0;
    }

    // MARKNN instruction

    if ((op & 0177700) == 0006400) {
    	word nn = op & 077;
    	r->r[6] += (nn << 1);
    	r->r[7] = r->r[5];
    	pullw(r->r[5]);
    	return 0;
    }

    // SOB instruction

    if ((op & 0177000) == 0077000) {
    	word nn = op & 077;
    	word reg = (op >> 6) & 07;
    	r->r[reg]--;
    	if (r->r[reg]) {
    		r->r[7] -= (nn << 1);
    	}
    	return 0;
    }

    //
    // single operand instructions
    //

#define GET_WORD(a) word a = get_data_word(r, dst_type, dst_offset);
#define GET_BYTE(a) word a = get_data_byte(r, dst_type, dst_offset);

#define PUT_WORD(a) put_data_word(r, dst_type, dst_offset, a);
#define PUT_BYTE(a) put_data_byte(r, dst_type, dst_offset, a);

    byte data_type = TYPE_WORD;

    if (op & 0100000) {
    	data_type = TYPE_BYTE;
    }

    //
    // only word operands
    // MTPS MFPS
    // SWAB SXT
    // JMP
    // XOR  JSR
    // ADD  SUB
    //
    if (((op & 0177700) == 0106400) || ((op & 0177700) == 0106700) ||
    	((op & 0177700) == 0000300) || ((op & 0177700) == 0006700) ||
    	((op & 0177700) == 0000100) ||
    	((op & 0177000) == 0074000) || ((op & 0177000) == 0004000) ||
    	((op & 0170000) == 0060000) || ((op & 0170000) == 0160000)) {
    	data_type = TYPE_WORD;
    }

    word dst_offset;
    byte dst_type = decode_data(r, op & 00077, data_type, &dst_offset);

    switch ((op & 0177700) >> 6) {
    case 00001: /* JMP */ {
    	r->r[7] = dst_offset;
    	}
    	return 0;

    case 00050: /* CLR */
    case 01050:
    	if (data_type == TYPE_WORD) {
    		put_data_word(r, dst_type, dst_offset, 0);
    	} else {
    		put_data_byte(r, dst_type, dst_offset, 0);
    	}
    	clear_flag(FLAG_N | FLAG_V | FLAG_C);
    	set_flag(FLAG_Z);
    	return 0;

    case 00051: /* COM */
    case 01051:
    	if (data_type == TYPE_WORD) {
    		GET_WORD(tmp);
    		tmp = ~tmp;
    		PUT_WORD(tmp);
    		set_flag_if(tmp == 0,   FLAG_Z);
    		set_flag_if(tmp & SIGN, FLAG_N);
    	} else {
    		GET_BYTE(tmp);
    		tmp = ~tmp;
    		PUT_BYTE(tmp);
    		set_flag_if(tmp == 0,     FLAG_Z);
    		set_flag_if(tmp & SIGN_B, FLAG_N);
    	}
    	clear_flag(FLAG_V);
    	set_flag(FLAG_C);
    	return 0;

    case 00052: /* INC */
    case 01052:
    	if (data_type == TYPE_WORD) {
    		GET_WORD(tmp);
    		set_flag_if(tmp == MPI,   FLAG_V);
    		tmp++;
    		PUT_WORD(tmp);
    		set_flag_if(tmp == 0,     FLAG_Z);
    		set_flag_if(tmp & SIGN,   FLAG_N);
    	} else {
    		GET_BYTE(tmp);
    		set_flag_if(tmp == MPI_B, FLAG_V);
    		tmp++;
    		PUT_BYTE(tmp);
    		set_flag_if(tmp == 0,     FLAG_Z);
    		set_flag_if(tmp & SIGN_B, FLAG_N);
    	}
    	return 0;

    case 00053: /* DEC */
    case 01053:
    	if (data_type == TYPE_WORD) {
    		GET_WORD(tmp);
    		set_flag_if(tmp == MNI,   FLAG_V);
    		tmp--;
    		PUT_WORD(tmp);
    		set_flag_if(tmp == 0,     FLAG_Z);
    		set_flag_if(tmp & SIGN,   FLAG_N);
    	} else {
    		GET_BYTE(tmp);
    		set_flag_if(tmp == MNI_B, FLAG_V);
    		tmp--;
    		PUT_BYTE(tmp);
    		set_flag_if(tmp == 0,     FLAG_Z);
    		set_flag_if(tmp & SIGN_B, FLAG_N);
    	}
    	return 0;

    case 00054: /* NEG */
    case 01054:
    	if (data_type == TYPE_WORD) {
    		GET_WORD(tmp);
    		tmp = (NEG_1 - tmp) + 1;
    		PUT_WORD(tmp);
    		set_flag_if(tmp == 0,     FLAG_Z);
    		set_flag_if(tmp & SIGN,   FLAG_N);
    		set_flag_if(tmp == MNI,   FLAG_V);
    		set_flag_if(tmp != 0,     FLAG_C);
    	} else {
    		GET_BYTE(tmp);
    		tmp = (NEG_1_B - tmp) + 1;
    		PUT_BYTE(tmp);
    		set_flag_if(tmp == 0,     FLAG_Z);
    		set_flag_if(tmp & SIGN_B, FLAG_N);
    		set_flag_if(tmp == MNI_B, FLAG_V);
    		set_flag_if(tmp != 0,     FLAG_C);
    	}
    	return 0;

    case 00057: /* TST */
    case 01057:
    	if (data_type == TYPE_WORD) {
    		GET_WORD(tmp);
    		set_flag_if(tmp == 0,     FLAG_Z);
    		set_flag_if(tmp & SIGN,   FLAG_N);
    	} else {
    		GET_BYTE(tmp);
    		set_flag_if(tmp == 0,     FLAG_Z);
    		set_flag_if(tmp & SIGN_B, FLAG_N);
    	}
    	clear_flag(FLAG_V | FLAG_C);
    	return 0;

    case 00062: /* ASR */
    case 01062:
    	if (data_type == TYPE_WORD) {
    		GET_WORD(tmp);
    		set_flag_if(tmp & 1,      FLAG_C);
    		tmp = (tmp & SIGN) | (tmp >> 1);
    		PUT_WORD(tmp);
    		set_flag_if(tmp & SIGN,   FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	} else {
    		GET_BYTE(tmp);
    		set_flag_if(tmp & 1,      FLAG_C);
    		tmp = (tmp & SIGN) | (tmp >> 1);
    		PUT_BYTE(tmp);
    		set_flag_if(tmp & SIGN_B, FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	}
    	set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
    	return 0;

    case 00063: /* ASR */
    case 01063:
    	if (data_type == TYPE_WORD) {
    		GET_WORD(tmp);
    		set_flag_if(tmp & SIGN,   FLAG_C);
    		tmp = tmp << 1;
    		PUT_WORD(tmp);
    		set_flag_if(tmp & SIGN,   FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	} else {
    		GET_BYTE(tmp);
    		set_flag_if(tmp & SIGN_B, FLAG_C);
    		tmp = tmp << 1;
    		PUT_BYTE(tmp);
    		set_flag_if(tmp & SIGN_B, FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	}
    	set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
    	return 0;

    case 00060: /* ROR */
    case 01060:
    	if (data_type == TYPE_WORD) {
    		GET_WORD(tmp);
    		word tmp_c = tmp & 1;
    		tmp = tmp >> 1;
    		if (flag_is_set(FLAG_C)) {
    			tmp |= SIGN;
    		}
    		PUT_WORD(tmp);
    		set_flag_if(tmp_c,        FLAG_C);
    		set_flag_if(tmp & SIGN,   FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	} else {
    		GET_BYTE(tmp);
    		byte tmp_c = tmp & 1;
    		tmp = tmp >> 1;
    		if (flag_is_set(FLAG_C)) {
    			tmp |= SIGN_B;
    		}
    		PUT_BYTE(tmp);
    		set_flag_if(tmp_c,        FLAG_C);
    		set_flag_if(tmp & SIGN_B, FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	}
    	set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
    	return 0;

    case 00061: /* ROL */
    case 01061:
    	if (data_type == TYPE_WORD) {
    		GET_WORD(tmp);
    		word tmp_c = tmp & SIGN;
    		tmp = tmp << 1;
    		if (flag_is_set(FLAG_C)) {
    			tmp |= 1;
    		}
    		PUT_WORD(tmp);
    		set_flag_if(tmp_c,        FLAG_C);
    		set_flag_if(tmp & SIGN,   FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	} else {
    		GET_BYTE(tmp);
    		byte tmp_c = tmp & SIGN_B;
    		tmp = tmp << 1;
    		if (flag_is_set(FLAG_C)) {
    			tmp |= 1;
    		}
    		PUT_BYTE(tmp);
    		set_flag_if(tmp_c,        FLAG_C);
    		set_flag_if(tmp & SIGN_B, FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	}
    	set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
    	return 0;

    case 00055: /* ADC */
    case 01055:
    	if (data_type == TYPE_WORD) {
    		GET_WORD(tmp);
    		set_flag_if((tmp == MPI) && flag_is_set(FLAG_C), FLAG_V);
    		byte tmp_c = (tmp == NEG_1) && flag_is_set(FLAG_C);
    		if (flag_is_set(FLAG_C)) {
    			tmp = tmp + 1;
    		}
    		PUT_WORD(tmp);
    		set_flag_if(tmp_c,        FLAG_C);
    		set_flag_if(tmp & SIGN,   FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	} else {
    		GET_BYTE(tmp);
    		set_flag_if((tmp == MPI_B) && flag_is_set(FLAG_C), FLAG_V);
    		byte tmp_c = (tmp == NEG_1_B) && flag_is_set(FLAG_C);
    		if (flag_is_set(FLAG_C)) {
    			tmp = tmp + 1;
    		}
    		PUT_BYTE(tmp);
    		set_flag_if(tmp_c,        FLAG_C);
    		set_flag_if(tmp & SIGN_B, FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	}
    	return 0;

    case 00056: /* SBC */
    case 01056:
    	if (data_type == TYPE_WORD) {
    		GET_WORD(tmp);
    		set_flag_if(tmp == MNI,   FLAG_V);
    		byte tmp_c = (tmp == 0) && flag_is_set(FLAG_C);
    		if (flag_is_set(FLAG_C)) {
    			tmp = tmp - 1;
    		}
    		PUT_WORD(tmp);
    		set_flag_if(!tmp_c,       FLAG_C);
    		set_flag_if(tmp & SIGN,   FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	} else {
    		GET_BYTE(tmp);
    		set_flag_if(tmp == MNI_B, FLAG_V);
    		byte tmp_c = (tmp == 0) && flag_is_set(FLAG_C);
    		if (flag_is_set(FLAG_C)) {
    			tmp = tmp - 1;
    		}
    		PUT_BYTE(tmp);
    		set_flag_if(!tmp_c,       FLAG_C);
    		set_flag_if(tmp & SIGN_B, FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	}
    	return 0;

    case 00003: /* SWAB */ {
    	GET_WORD(tmp);
    	tmp = (tmp >> 8) | (tmp << 8);
    	PUT_WORD(tmp);
    	tmp &= 0377;
    	clear_flag(FLAG_V | FLAG_C);
    	set_flag_if(tmp & SIGN_B,     FLAG_N);
    	set_flag_if(tmp == 0,         FLAG_Z);
		}
		return 0;

    case 00067: /* SXT */
    	if (flag_is_set(FLAG_N)) {
    		put_data_word(r, dst_type, dst_offset, NEG_1);
    		clear_flag(FLAG_Z);
    	} else {
    		put_data_word(r, dst_type, dst_offset, 0);
    		set_flag(FLAG_Z);
    	}
    	clear_flag(FLAG_V);
    	return 0;

    case 01067: /* MFPS */ {
#warning MFPS
    	word tmp = r->psw;
    	PUT_WORD(tmp);
    	set_flag_if(tmp & SIGN_B,      FLAG_N);
    	set_flag_if((tmp & 0377) == 0, FLAG_Z);
    	clear_flag(FLAG_V);
		}
    	return 0;

    case 01064: /* MTPS */
#warning MTPS
    	r->psw = (get_data_word(r, dst_type, dst_offset) & 0007) | (r->psw & 0177770);
    	return 0;
    }

    //
    // Double operand instructions - register and address
    //

    byte reg = (op >> 6) & 07;

    switch((op & 0177000) >> 9) {
    case 0004: /* JSR */
    	pushw(r->r[reg]);
    	r->r[reg] = r->r[7];
    	r->r[7] = dst_offset;
    	return 0;

    case 0074: /* XOR */ {
    	GET_WORD(tmp);
    	tmp ^= r->r[reg];
    	PUT_WORD(tmp);
		set_flag_if(tmp & SIGN,   FLAG_N);
		set_flag_if(tmp == 0,     FLAG_Z);
		clear_flag(FLAG_V);
    	}
    	return 0;
    }

    //
    // Double operand instructions
    //

#define GET_SWORD(a) word a = get_data_word(r, src_type, src_offset);
#define GET_SBYTE(a) word a = get_data_byte(r, src_type, src_offset);

    word src_offset;
    byte src_type = decode_data(r, (op & 07700) >> 6, data_type, &src_offset);

    switch((op & 0170000) >> 12) {
    case 001: /* MOV */
    case 011:
    	if (data_type == TYPE_WORD) {
    		GET_SWORD(tmp);
    		PUT_WORD(tmp);
    		set_flag_if(tmp & SIGN,   FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	} else {
    		GET_SBYTE(tmp);
    		PUT_BYTE(tmp);
    		set_flag_if(tmp & SIGN_B, FLAG_N);
    		set_flag_if(tmp == 0,     FLAG_Z);
    	}
		clear_flag(FLAG_V);
    	return 0;

    case 002: /* CMP */
    case 012:
    	if (data_type == TYPE_WORD) {
    		GET_SWORD(tmp);
    		GET_WORD(tmp1);
    		word tmp2 = ~tmp1;
    		unsigned long tmp3 = ((unsigned long) tmp) + ((unsigned long) tmp2) + 1;
    		tmp2 = tmp3 & 0177777;
    		set_flag_if(tmp2 & SIGN,  FLAG_N);
    		set_flag_if(tmp2 == 0,    FLAG_Z);
    		set_flag_if(((tmp & SIGN) != (tmp1 & SIGN)) && ((tmp1 & SIGN) == (tmp2 & SIGN)), FLAG_V);
    		set_flag_if(!(tmp3 & CARRY), FLAG_C);
    	} else {
    		GET_SBYTE(tmp);
    		GET_BYTE(tmp1);
    		byte tmp2 = ~tmp1;
    		unsigned long tmp3 = ((unsigned long) tmp) + ((unsigned long) tmp2) + 1;
    		tmp2 = tmp3 & 0377;
    		set_flag_if(tmp2 & SIGN_B,  FLAG_N);
    		set_flag_if(tmp2 == 0,      FLAG_Z);
    		set_flag_if(((tmp & SIGN_B) != (tmp1 & SIGN_B)) && ((tmp1 & SIGN_B) == (tmp2 & SIGN_B)), FLAG_V);
    		set_flag_if(!(tmp3 & CARRY_B), FLAG_C);
    	}
    	return 0;

    case 006: /* ADD */ {
    	GET_SWORD(tmp);
    	GET_WORD(tmp1);
    	unsigned long tmp3 = ((unsigned long) tmp) + ((unsigned long) tmp1);
    	word tmp2 = tmp3 & 0177777;
    	PUT_WORD(tmp2);
		set_flag_if(tmp2 & SIGN,  FLAG_N);
		set_flag_if(tmp2 == 0,    FLAG_Z);
		set_flag_if(((tmp & SIGN) == (tmp1 & SIGN)) && ((tmp & SIGN) != (tmp2 & SIGN)), FLAG_V);
		set_flag_if(tmp3 & CARRY, FLAG_C);
    	}
    	return 0;

    case 016: /* SUB */ {
    	GET_SWORD(tmp);
    	GET_WORD(tmp1);
    	word tmp2 = ~tmp;
    	unsigned long tmp3 = ((unsigned long) tmp1) + ((unsigned long) tmp2) + 1;
    	tmp2 = tmp3 & 0177777;
    	PUT_WORD(tmp2);
		set_flag_if(tmp2 & SIGN,  FLAG_N);
		set_flag_if(tmp2 == 0,    FLAG_Z);
		set_flag_if(((tmp & SIGN) != (tmp1 & SIGN)) && ((tmp & SIGN) == (tmp2 & SIGN)), FLAG_V);
		set_flag_if(!(tmp3 & CARRY), FLAG_C);
    	}
    	return 0;

    case 003: /* BIT */
    case 013:
    	if (data_type == TYPE_WORD) {
    		GET_SWORD(tmp);
    		GET_WORD(tmp1);
    		word tmp2 = tmp & tmp1;
    		set_flag_if(tmp2 & SIGN,   FLAG_N);
    		set_flag_if(tmp2 == 0,     FLAG_Z);
    	} else {
    		GET_SBYTE(tmp);
    		GET_BYTE(tmp1);
    		byte tmp2 = tmp & tmp1;
    		set_flag_if(tmp2 & SIGN_B, FLAG_N);
    		set_flag_if(tmp2 == 0,     FLAG_Z);
    	}
		clear_flag(FLAG_V);
    	return 0;

    case 004: /* BIC */
    case 014:
    	if (data_type == TYPE_WORD) {
    		GET_SWORD(tmp);
    		GET_WORD(tmp1);
    		tmp1 = (~tmp) & tmp1;
    		PUT_WORD(tmp1);
    		set_flag_if(tmp1 & SIGN,   FLAG_N);
    		set_flag_if(tmp1 == 0,     FLAG_Z);
    	} else {
    		GET_SBYTE(tmp);
    		GET_BYTE(tmp1);
    		tmp1 = (~tmp) & tmp1;
    		PUT_BYTE(tmp1);
    		set_flag_if(tmp1 & SIGN_B, FLAG_N);
    		set_flag_if(tmp1 == 0,     FLAG_Z);
    	}
		clear_flag(FLAG_V);
    	return 0;

    case 005: /* BIS */
    case 015:
    	if (data_type == TYPE_WORD) {
    		GET_SWORD(tmp);
    		GET_WORD(tmp1);
    		tmp1 = tmp | tmp1;
    		PUT_WORD(tmp1);
    		set_flag_if(tmp1 & SIGN,   FLAG_N);
    		set_flag_if(tmp1 == 0,     FLAG_Z);
    	} else {
    		GET_SBYTE(tmp);
    		GET_BYTE(tmp1);
    		tmp1 = tmp | tmp1;
    		PUT_BYTE(tmp1);
    		set_flag_if(tmp1 & SIGN_B, FLAG_N);
    		set_flag_if(tmp1 == 0,     FLAG_Z);
    	}
		clear_flag(FLAG_V);
    	return 0;
    }

    return -1;
}
