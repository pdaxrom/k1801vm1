/*
 * core.c
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#include <stdio.h>
#include "core.h"
#include "hardware.h"

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

#define is_vm2(r) ((r)->model == K1801VM2 || (r)->model == K1806VM2)
static INLINE int dcj11_kernel_psw(word psw)
{
	return ((psw >> 14) & 03) == 0;
}

#define pushw(v) {			\
	r->r[6] -= 2;			\
	store_word(r, r->r[6], v); \
}

#define pullw(v) {			\
	v = load_word(r, r->r[6]); \
	r->r[6] += 2;			\
}

static INLINE int irq_accept(regs *r, word irq_vector, word *vec_out)
{
	word vec = (r->model == K1801VM1 || r->model == K1801VM1G)
		? irq_vector
		: (irq_vector & 0777);
	int pri = (irq_vector >> 9) & 07;
	int psw_pri = (r->psw >> 5) & 07;
	if (r->model == K1801VM1 || r->model == K1801VM1G) {
		const word psw10 = 01000;
		const word psw11 = 02000;
		if (r->psw & psw10) {
			return 0;
		}
		if ((vec == 0160002) && (r->psw & psw11)) {
			return 0;
		}
		if ((vec != 0160002) && flag_is_set(FLAG_P)) {
			return 0;
		}
		*vec_out = vec & 0177776;
		return 1;
	}
	if (is_vm2(r)) {
		vec = irq_vector;
		if (r->psw & FLAG_P) {
			return 0;
		}
		*vec_out = vec & 0177776;
		return 1;
	}
	if (pri && psw_pri >= pri) {
		return 0;
	}
	*vec_out = vec & 017776;
	return 1;
}


void core_init(regs *r)
{
	if (r->model == K1806VM2) {
		r->model = K1801VM2;
	}
	r->init(r);
}

void core_reset(regs *r)
{
    r->r[7] = r->SEL0 & 0177400;
    r->psw = 0340;
    r->ir = 0;
    r->fWait = 0;
    r->fTrap = 0;
    r->fAbort = 0;
    r->fHaltSignal = 0;
    r->fStepDeferHalt = 0;
    r->fFisError = 0;

    r->reset(r);
}

void core_fini(regs *r)
{
	if (r->fini) {
		r->fini(r);
	}
}

#define load_byte(a, b) r->load_byte(a, b)
#define store_byte(a, b, c) r->store_byte(a, b, c)

static INLINE void bus_error_trap(regs *r);

static INLINE word core_load_word(regs *r, word offset)
{
	if ((r->model == DCJ11) && (offset & 1)) {
		bus_error_trap(r);
		return 0;
	}
	return r->load_word(r, offset);
}

static INLINE void core_store_word(regs *r, word offset, word value)
{
	if ((r->model == DCJ11) && (offset & 1)) {
		bus_error_trap(r);
		return;
	}
	r->store_word(r, offset, value);
}

#define load_word(a, b) core_load_word(a, b)
#define store_word(a, b, c) core_store_word(a, b, c)

static INLINE void illegal_trap(regs *r)
{
	pushw(r->psw);
	pushw(r->r[7]);
	r->r[7] = load_word(r, 010);
	r->psw  = load_word(r, 012);
}

#undef load_word
#undef store_word

static INLINE void bus_error_trap(regs *r)
{
	r->r[6] -= 2;
	r->store_word(r, r->r[6], r->psw);
	r->r[6] -= 2;
	r->store_word(r, r->r[6], r->r[7]);
	r->r[7] = r->load_word(r, 000004);
	r->psw  = r->load_word(r, 000006);
	r->fAbort = 1;
}

#define load_word(a, b) core_load_word(a, b)
#define store_word(a, b, c) core_store_word(a, b, c)

static INLINE void handle_halt(regs *r)
{
	word vec;
	if (r->model == K1801VM1 || r->model == K1801VM1G) {
		store_word(r, 0177676, r->psw);
		store_word(r, 0177674, r->r[7]);
		store_word(r, 0177716, load_word(r, 0177716) | 010);
		vec = 0160002;
		r->r[7] = load_word(r, vec);
		r->psw  = load_word(r, (word)(vec + 2));
	} else if (is_vm2(r)) {
		r->cps = r->psw;
		r->cpc = r->r[7];
		vec = (word)((r->SEL0 & 0177400) | 0170);
		r->r[7] = load_word(r, vec    ) & 0177776;
		r->psw  = load_word(r, vec + 2);
	} else if (r->model == DCJ11) {
		pushw(r->psw);
		pushw(r->r[7]);
		vec = 4;
		if (flag_is_set(FLAG_H)) {
			vec |= (r->SEL0 & 0177400);
		}
		r->r[7] = load_word(r, vec    ) & 0177776;
		r->psw  = 0340;
	} else {
		store_word(r, 0177676, r->psw);
		store_word(r, 0177674, r->r[7]);
		store_word(r, 0177716, load_word(r, 0177716) | 010);
		vec = (r->SEL0 & 0177400);
		r->r[7] = load_word(r, vec + 2) & 0177776;
		r->psw  = load_word(r, vec + 4);
	}
}

static INLINE void handle_fis(regs *r)
{
	word vec;
	if (r->SEL0 & 0200) {
		illegal_trap(r);
		return;
	}
	r->cps = r->psw;
	r->cpc = r->r[7];
	vec = (word)((r->SEL0 & 0177400) | 010);
	r->r[7] = load_word(r, vec    ) & 0177776;
	r->psw  = load_word(r, vec + 2);
}

static INLINE void handle_fis_error(regs *r)
{
	pushw(r->psw);
	pushw(r->r[7]);
	r->r[7] = load_word(r, 0244);
	r->psw  = load_word(r, 0246);
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

static INLINE void put_data_byte_movb(regs *r, byte type, word offset, byte value) {
	if (type == TYPE_REG) {
		r->r[offset] = (value & SIGN_B) ? (0177400 | value) : (value & 0377);
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

    if (r->fAbort) {
    	return TYPE_ERROR;
    }

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
    	if (r->fAbort) {
    		return TYPE_ERROR;
    	}
    	r->r[reg] += step;
    	return TYPE_MEM;
    case 4: /* -(Rn) */
    	r->r[reg] -= step;
    	*offset = r->r[reg];
    	return TYPE_MEM;
    case 5: /* @-(Rn) */
    	r->r[reg] -= step;
    	*offset = load_word(r, r->r[reg]);
    	if (r->fAbort) {
    		return TYPE_ERROR;
    	}
    	return TYPE_MEM;
    case 6: /* X(Rn) */ {
    	word tmp = load_word(r, r->r[7]);
    	if (r->fAbort) {
    		return TYPE_ERROR;
    	}
    	r->r[7] += 2;
    	*offset = r->r[reg] + tmp;
    	}
    	return TYPE_MEM;
    case 7: /* @X(Rn) */ {
    	word tmp = load_word(r, r->r[7]);
    	if (r->fAbort) {
    		return TYPE_ERROR;
    	}
    	r->r[7] += 2;
    	*offset = load_word(r, r->r[reg] + tmp);
    	if (r->fAbort) {
    		return TYPE_ERROR;
    	}
    	}
    	return TYPE_MEM;
    }

    // Never return here.
    return TYPE_ERROR;
}

int core_step(regs *r)
{
    word src_offset;
    byte src_type;
    word dst_offset;
    byte dst_type;
    word irq_vector;
    word psw_before = r->psw;
    int do_trace = 0;
    int defer_halt = 0;
    int skip_irq = 0;
    int skip_trace = r->fTrap ? 1 : 0;
    r->fTrap = 0;

    if (r->fAbort) {
    	r->fAbort = 0;
    	return 0;
    }

    if (r->fHaltSignal) {
        if (is_vm2(r) && r->fStepDeferHalt) {
            r->fStepDeferHalt = 0;
            defer_halt = 1;
        } else {
            r->fHaltSignal = 0;
            if (is_vm2(r) && flag_is_set(FLAG_H)) {
                r->fWait = 0;
                return 0;
            }
            r->fWait = 0;
            handle_halt(r);
            return 0;
        }
    }

    if (r->fWait) {
        if (r->poll_irq && r->poll_irq(r, &irq_vector)) {
            word vec;
            if (irq_accept(r, irq_vector, &vec)) {
                r->fWait = 0;
                pushw(r->psw);
                pushw(r->r[7]);
                r->r[7] = load_word(r, vec);
                r->psw  = load_word(r, (word)(vec + 2));
            }
            return 0;
        }
    	return 0;
    }

    /* IRQs are checked after instruction execution (real PDP-11 behavior). */


    // load instruction

	word op = load_word(r, r->r[7]);
	if (r->fAbort) {
		r->fAbort = 0;
        return 0;
    }
    r->ir = op;
    r->r[7] += 2;
    if (is_vm2(r) && ((op & 0177700) == 0075000)) {
        if (r->fFisError) {
            r->fFisError = 0;
            handle_fis_error(r);
        } else {
            handle_fis(r);
        }
        goto step_end;
    }

    //
    // No operands instructions
    //
    switch(op) {
		case 000000: /* HALT */ {
			if (r->model == DCJ11 && !dcj11_kernel_psw(psw_before)) {
				pushw(r->psw);
				pushw(r->r[7]);
				r->r[7] = load_word(r, 000004);
				r->psw  = load_word(r, 000006);
				goto step_end;
			}
			handle_halt(r);
        goto step_end;
		}

		case 000001: /* WAIT */
			r->fWait = 1;
			if (r->model == K1801VM1 || r->model == K1801VM1G || is_vm2(r) || r->model == DCJ11) {
				skip_trace = 1;
			}
        goto step_end;

		case 000002: /* RTI */
			pullw(r->r[7]);
			pullw(r->psw);
			if (is_vm2(r)) {
				if (r->r[7] < 0160000) {
					r->psw = (word)((r->psw & ~FLAG_H) | (psw_before & FLAG_H));
				}
			}
			if (r->model == DCJ11 && !dcj11_kernel_psw(psw_before)) {
				word mask = 0170000 | 000340;
				r->psw = (word)((r->psw & ~mask) | (psw_before & mask));
			}
        goto step_end;

		case 000003: /* BPT */
			pushw(r->psw);
			pushw(r->r[7]);
			r->r[7] = load_word(r, 014);
			r->psw  = load_word(r, 016);
        goto step_end;

		case 000004: /* IOT */
			pushw(r->psw);
			pushw(r->r[7]);
			r->r[7] = load_word(r, 020);
			r->psw  = load_word(r, 022);
        goto step_end;

		case 000005: /* RESET */
			if (r->model == DCJ11 && !dcj11_kernel_psw(psw_before)) {
				goto step_end;
			}
			r->fWait = 0;
			r->fHaltSignal = 0;
			if (r->reset) {
				r->reset(r);
			}
        goto step_end;

		case 000006: /* RTT */
			pullw(r->r[7]);
			pullw(r->psw);
			if (is_vm2(r)) {
				if (r->r[7] < 0160000) {
					r->psw = (word)((r->psw & ~FLAG_H) | (psw_before & FLAG_H));
				}
			}
			if (r->model == DCJ11 && !dcj11_kernel_psw(psw_before)) {
				word mask = 0170000 | 000340;
				r->psw = (word)((r->psw & ~mask) | (psw_before & mask));
			}
			r->fTrap = 1;
			skip_trace = 1;
        goto step_end;

		case 000007: /* MFPT */
			if (r->model != DCJ11) {
				illegal_trap(r);
        goto step_end;
			}
			r->r[0] = 5;
        goto step_end;

		case 000240: /* NOP */
        goto step_end;

		case 000241: /* CLC */
			clear_flag(FLAG_C);
        goto step_end;

		case 000242: /* CLV */
			clear_flag(FLAG_V);
        goto step_end;

		case 000244: /* CLZ */
			clear_flag(FLAG_Z);
        goto step_end;

		case 000250: /* CLN */
			clear_flag(FLAG_N);
        goto step_end;

		case 000257: /* CCC */
			clear_flag(FLAG_C | FLAG_V | FLAG_Z | FLAG_N);
        goto step_end;

		case 000261: /* SEC */
			set_flag(FLAG_C);
        goto step_end;

		case 000262: /* SEV */
			set_flag(FLAG_V);
        goto step_end;

		case 000264: /* SEZ */
			set_flag(FLAG_Z);
        goto step_end;

		case 000270: /* SEN */
			set_flag(FLAG_N);
        goto step_end;

		case 000277: /* SCC */
			set_flag(FLAG_C | FLAG_V | FLAG_Z | FLAG_N);
        goto step_end;

    }

    if (is_vm2(r)) {
    	switch(op) {
    	case 0000012: /* START */
            if (!flag_is_set(FLAG_H) || !flag_is_set(FLAG_P)) {
                illegal_trap(r);
        goto step_end;
            }
    		r->r[7] = r->cpc;
    		r->psw = r->cps;
            if (r->poll_irq) {
                word vec;
                word irq_vector;
                if (r->poll_irq(r, &irq_vector)) {
                    if (irq_accept(r, irq_vector, &vec)) {
                        pushw(r->psw);
                        pushw(r->r[7]);
                        r->r[7] = load_word(r, vec);
                        r->psw  = load_word(r, (word)(vec + 2));
                    }
                }
            }
        goto step_end;

    	case 0000016: /* STEP */
            if (!flag_is_set(FLAG_H) || !flag_is_set(FLAG_P)) {
                illegal_trap(r);
        goto step_end;
            }
    		r->r[7] = r->cpc;
    		r->psw = r->cps;
            skip_irq = 1;
            r->fStepDeferHalt = 1;
        goto step_end;

    	case 0000020: /* RSEL */
            if (!flag_is_set(FLAG_H)) {
                illegal_trap(r);
        goto step_end;
            }
    		r->r[0] = r->SEL0;
        goto step_end;

    	case 0000021: /* MFUS */
            if (!flag_is_set(FLAG_H)) {
                illegal_trap(r);
        goto step_end;
            }
            {
                word psw_saved = r->psw;
                r->psw = (word)(r->psw & ~FLAG_H);
    		r->r[0] = load_word(r, r->r[5]);
    		r->r[5] += 2;
                r->psw = psw_saved;
            }
        goto step_end;

    	case 0000022: /* RCPC */
            if (!flag_is_set(FLAG_H) || !flag_is_set(FLAG_P)) {
                illegal_trap(r);
        goto step_end;
            }
    		r->r[0] = r->cpc;
        goto step_end;

    	case 0000024: /* RCPS */
            if (!flag_is_set(FLAG_H) || !flag_is_set(FLAG_P)) {
                illegal_trap(r);
        goto step_end;
            }
    		r->r[0] = r->cps;
        goto step_end;

    	case 0000031: /* MTUS */
            if (!flag_is_set(FLAG_H)) {
                illegal_trap(r);
        goto step_end;
            }
            {
                word psw_saved = r->psw;
                r->psw = (word)(r->psw & ~FLAG_H);
    		r->r[5] -= 2;
    		store_word(r, r->r[5], r->r[0]);
                r->psw = psw_saved;
            }
        goto step_end;

    	case 0000032: /* WCPC */
            if (!flag_is_set(FLAG_H) || !flag_is_set(FLAG_P)) {
                illegal_trap(r);
        goto step_end;
            }
    		r->cpc = r->r[0];
        goto step_end;

    	case 0000034: /* WCPS */
            if (!flag_is_set(FLAG_H) || !flag_is_set(FLAG_P)) {
                illegal_trap(r);
        goto step_end;
            }
    		r->cps = r->r[0];
        goto step_end;
    	}
    }

#define BR_OFFSET()			\
    word offset = op & 0377;\
    if (offset & SIGN_B) {	\
    	offset += 0177400;	\
    }

    switch (op & 0177400) {
		case 0000400: /* BR */ {
			BR_OFFSET();
			r->r[7] += (offset * 2);
        goto step_end;
		}

		case 0001000: /* BNE */
			if (flag_is_clear(FLAG_Z)) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0001400: /* BEQ */
			if (flag_is_set(FLAG_Z)) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0100000: /* BPL */
			if (flag_is_clear(FLAG_N)) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0100400: /* BMI */
			if (flag_is_set(FLAG_N)) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0102000: /* BVC */
			if (flag_is_clear(FLAG_V)) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0102400: /* BVS */
			if (flag_is_set(FLAG_V)) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0103000: /* BCC or BHIS */
			if (flag_is_clear(FLAG_C)) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0103400: /* BCS or BLO */
			if (flag_is_set(FLAG_C)) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0002000: /* BGE */
			if ((flag_is_set(FLAG_N) ^ flag_is_set(FLAG_V)) == 0) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0002400: /* BLT */
			if ((flag_is_set(FLAG_N) ^ flag_is_set(FLAG_V)) == 1) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0003000: /* BGT */
			if (((flag_is_set(FLAG_N) ^ flag_is_set(FLAG_V)) | flag_is_set(FLAG_Z)) == 0) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0003400: /* BLE */
			if (((flag_is_set(FLAG_N) ^ flag_is_set(FLAG_V)) | flag_is_set(FLAG_Z)) == 1) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0101000: /* BHI */
			if (flag_is_clear(FLAG_C | FLAG_Z)) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		case 0101400: /* BLOS */
			if (!flag_is_clear(FLAG_C | FLAG_Z)) {
				BR_OFFSET();
				r->r[7] += (offset * 2);
			}
        goto step_end;

		/* EMT */
		case 0104000:
		/* TRAP */
		case 0104400: {
			word vec = (op & 0400) ? 000034 : 000030;
			pushw(r->psw);
			pushw(r->r[7]);
			r->r[7] = load_word(r, vec);
			r->psw  = load_word(r, (word)(vec + 2));
		}
        goto step_end;
    }

    // RTS
    if ((op & 0177770) == 000200) {
    	byte reg = op & 07;
    	r->r[7] = r->r[reg];
    	pullw(r->r[reg]);
        goto step_end;
    }

    // MARKNN instruction

    if ((op & 0177700) == 0006400) {
    	word nn = op & 077;
    	r->r[6] += (nn << 1);
    	r->r[7] = r->r[5];
    	pullw(r->r[5]);
        goto step_end;
    }

    // SOB instruction

    if ((op & 0177000) == 0077000) {
    	word nn = op & 077;
    	word reg = (op >> 6) & 07;
    	r->r[reg]--;
    	if (r->r[reg]) {
    		r->r[7] -= (nn << 1);
    	}
        goto step_end;
    }

    //
    // single operand instructions
    //

#define DECODE_DST()  do { dst_type = decode_data(r, op & 00077, TYPE_WORD, &dst_offset); if (dst_type == TYPE_ERROR) return 0; } while (0)
#define DECODE_DSTB() do { dst_type = decode_data(r, op & 00077, TYPE_BYTE, &dst_offset); if (dst_type == TYPE_ERROR) return 0; } while (0)

#define GET_WORD(a) word a = get_data_word(r, dst_type, dst_offset); if (r->fAbort) return 0;
#define GET_BYTE(a) word a = get_data_byte(r, dst_type, dst_offset); if (r->fAbort) return 0;

#define PUT_WORD(a) put_data_word(r, dst_type, dst_offset, a); if (r->fAbort) return 0;
#define PUT_BYTE(a) put_data_byte(r, dst_type, dst_offset, a); if (r->fAbort) return 0;
#define PUT_BYTE_MOVB(a) put_data_byte_movb(r, dst_type, dst_offset, a); if (r->fAbort) return 0;

    switch ((op & 0177700) >> 6) {
		case 00001: /* JMP */
			if ((op & 070) == 0) {
				illegal_trap(r);
        goto step_end;
			}
			DECODE_DST();
			r->r[7] = dst_offset;
        goto step_end;

		case 00050: /* CLR */
			DECODE_DST();
			put_data_word(r, dst_type, dst_offset, 0);
			clear_flag(FLAG_N | FLAG_V | FLAG_C);
			set_flag(FLAG_Z);
        goto step_end;
		case 01050: /* CLRB */
			DECODE_DSTB();
			put_data_byte(r, dst_type, dst_offset, 0);
			clear_flag(FLAG_N | FLAG_V | FLAG_C);
			set_flag(FLAG_Z);
        goto step_end;

		case 00051: /* COM */ {
			DECODE_DST();
			GET_WORD(tmp);
			tmp = ~tmp;
			PUT_WORD(tmp);
			set_flag_if(tmp == 0,   FLAG_Z);
			set_flag_if(tmp & SIGN, FLAG_N);
			clear_flag(FLAG_V);
			set_flag(FLAG_C);
        goto step_end;
		}
		case 01051: /* COMB */ {
			DECODE_DSTB();
			GET_BYTE(tmp);
			tmp = (~tmp) & 0377;
			PUT_BYTE(tmp);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(tmp & SIGN_B, FLAG_N);
			clear_flag(FLAG_V);
			set_flag(FLAG_C);
        goto step_end;
		}

		case 00052: /* INC */ {
			DECODE_DST();
			GET_WORD(tmp);
			set_flag_if(tmp == MPI,   FLAG_V);
			tmp++;
			PUT_WORD(tmp);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(tmp & SIGN,   FLAG_N);
        goto step_end;
		}
		case 01052: /* INCB */ {
			DECODE_DSTB();
			GET_BYTE(tmp);
			set_flag_if(tmp == MPI_B, FLAG_V);
			tmp = (tmp + 1) & 0377;
			PUT_BYTE(tmp);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(tmp & SIGN_B, FLAG_N);
        goto step_end;
		}

		case 00053: /* DEC */ {
			DECODE_DST();
			GET_WORD(tmp);
			set_flag_if(tmp == MNI,   FLAG_V);
			tmp--;
			PUT_WORD(tmp);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(tmp & SIGN,   FLAG_N);
        goto step_end;
		}
		case 01053: /* DECB */ {
			DECODE_DSTB();
			GET_BYTE(tmp);
			set_flag_if(tmp == MNI_B, FLAG_V);
			tmp = (tmp - 1) & 0377;
			PUT_BYTE(tmp);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(tmp & SIGN_B, FLAG_N);
        goto step_end;
		}

		case 00054: /* NEG */ {
			DECODE_DST();
			GET_WORD(tmp);
			tmp = (NEG_1 - tmp) + 1;
			PUT_WORD(tmp);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(tmp & SIGN,   FLAG_N);
			set_flag_if(tmp == MNI,   FLAG_V);
			set_flag_if(tmp != 0,     FLAG_C);
        goto step_end;
		}
		case 01054: /* NEGB */ {
			DECODE_DSTB();
			GET_BYTE(tmp);
			tmp = ((NEG_1_B - tmp) + 1) & 0377;
			PUT_BYTE(tmp);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(tmp & SIGN_B, FLAG_N);
			set_flag_if(tmp == MNI_B, FLAG_V);
			set_flag_if(tmp != 0,     FLAG_C);
        goto step_end;
		}

		case 00057: /* TST */ {
			DECODE_DST();
			GET_WORD(tmp);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(tmp & SIGN,   FLAG_N);
			clear_flag(FLAG_V | FLAG_C);
        goto step_end;
		}
		case 01057: /* TSTB */ {
			DECODE_DSTB();
			GET_BYTE(tmp);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(tmp & SIGN_B, FLAG_N);
			clear_flag(FLAG_V | FLAG_C);
        goto step_end;
		}

		case 00062: /* ASR */ {
			DECODE_DST();
			GET_WORD(tmp);
			set_flag_if(tmp & 1,      FLAG_C);
			tmp = (tmp & SIGN) | (tmp >> 1);
			PUT_WORD(tmp);
			set_flag_if(tmp & SIGN,   FLAG_N);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
		}
		case 01062: /* ASRB */ {
			DECODE_DSTB();
			GET_BYTE(tmp);
			set_flag_if(tmp & 1,      FLAG_C);
			tmp = (tmp & SIGN_B) | (tmp >> 1);
			PUT_BYTE(tmp);
			set_flag_if(tmp & SIGN_B, FLAG_N);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
		}

		case 00063: /* ASL */ {
			DECODE_DST();
			GET_WORD(tmp);
			set_flag_if(tmp & SIGN,   FLAG_C);
			tmp = tmp << 1;
			PUT_WORD(tmp);
			set_flag_if(tmp & SIGN,   FLAG_N);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
		}
		case 01063: /* ASLB */ {
			DECODE_DSTB();
			GET_BYTE(tmp);
			set_flag_if(tmp & SIGN_B, FLAG_C);
			tmp = (tmp << 1) & 0377;
			PUT_BYTE(tmp);
			set_flag_if(tmp & SIGN_B, FLAG_N);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
		}

		case 00060: /* ROR */ {
			DECODE_DST();
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
			set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
		}
		case 01060: /* RORB */ {
			DECODE_DSTB();
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
			set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
		}

		case 00061: /* ROL */ {
			DECODE_DST();
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
			set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
		}
		case 01061: /* ROLB */ {
			DECODE_DSTB();
			GET_BYTE(tmp);
			byte tmp_c = tmp & SIGN_B;
			tmp = (tmp << 1) & 0377;
			if (flag_is_set(FLAG_C)) {
				tmp |= 1;
			}
			tmp &= 0377;
			PUT_BYTE(tmp);
			set_flag_if(tmp_c,        FLAG_C);
			set_flag_if(tmp & SIGN_B, FLAG_N);
			set_flag_if(tmp == 0,     FLAG_Z);
			set_flag_if(flag_is_set(FLAG_N) ^ flag_is_set(FLAG_C), FLAG_V);
        goto step_end;
		}

		case 00055: /* ADC */ {
			DECODE_DST();
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
        goto step_end;
		}
		case 01055: /* ADCB */ {
			DECODE_DSTB();
			GET_BYTE(tmp);
			set_flag_if((tmp == MPI_B) && flag_is_set(FLAG_C), FLAG_V);
			byte tmp_c = (tmp == NEG_1_B) && flag_is_set(FLAG_C);
			if (flag_is_set(FLAG_C)) {
				tmp = (tmp + 1) & 0377;
			}
			PUT_BYTE(tmp);
			set_flag_if(tmp_c,        FLAG_C);
			set_flag_if(tmp & SIGN_B, FLAG_N);
			set_flag_if(tmp == 0,     FLAG_Z);
        goto step_end;
		}

		case 00056: /* SBC */ {
			DECODE_DST();
			GET_WORD(tmp);
			set_flag_if(tmp == MNI,   FLAG_V);
			byte flag_c = flag_is_set(FLAG_C);
			byte tmp_c = flag_c && (tmp == 0);
			if (flag_c) {
				tmp = tmp - 1;
			}
			PUT_WORD(tmp);
			set_flag_if(tmp_c,        FLAG_C);
			set_flag_if(tmp & SIGN,   FLAG_N);
			set_flag_if(tmp == 0,     FLAG_Z);
        goto step_end;
		}
		case 01056: /* SBCB */ {
			DECODE_DSTB();
			GET_BYTE(tmp);
			set_flag_if(tmp == MNI_B, FLAG_V);
			byte flag_c = flag_is_set(FLAG_C);
			byte tmp_c = flag_c && (tmp == 0);
			if (flag_c) {
				tmp = (tmp - 1) & 0377;
			}
			PUT_BYTE(tmp);
			set_flag_if(tmp_c,        FLAG_C);
			set_flag_if(tmp & SIGN_B, FLAG_N);
			set_flag_if(tmp == 0,     FLAG_Z);
        goto step_end;
		}

		case 00003: /* SWAB */ {
			DECODE_DST();
			GET_WORD(tmp);
			tmp = (tmp >> 8) | (tmp << 8);
			PUT_WORD(tmp);
			tmp &= 0377;
			clear_flag(FLAG_V | FLAG_C);
			set_flag_if(tmp & SIGN_B,     FLAG_N);
			set_flag_if(tmp == 0,         FLAG_Z);
			}
        goto step_end;

		case 00067: /* SXT */
			DECODE_DST();
			if (flag_is_set(FLAG_N)) {
				put_data_word(r, dst_type, dst_offset, NEG_1);
				clear_flag(FLAG_Z);
			} else {
				put_data_word(r, dst_type, dst_offset, 0);
				set_flag(FLAG_Z);
			}
			clear_flag(FLAG_V);
        goto step_end;

		case 01067: /* MFPS */ {
			DECODE_DST();
			word tmp = r->psw;
			PUT_WORD(tmp);
			set_flag_if(tmp & SIGN_B,      FLAG_N);
			set_flag_if((tmp & 0377) == 0, FLAG_Z);
			clear_flag(FLAG_V);
			}
        goto step_end;

		case 01064: /* MTPS */
			DECODE_DSTB();
			GET_BYTE(tmp);
			{
				word low = tmp & 0357;
				if (r->model == DCJ11 && !dcj11_kernel_psw(psw_before)) {
					low = (word)((low & 000037) | (r->psw & 000340));
				}
				r->psw = (word)((r->psw & 0177000) | (r->psw & (FLAG_T | FLAG_H)) | low);
			}
        goto step_end;

		case 00072: /* TSTSET */ {
			if (r->model != DCJ11) {
				illegal_trap(r);
        goto step_end;
			}
			if ((op & 070) == 0) {
				illegal_trap(r);
        goto step_end;
			}
			DECODE_DST();
			GET_WORD(tmp);
			r->r[0] = tmp;
			PUT_WORD(tmp | 1);
			set_flag_if(r->r[0] & SIGN, FLAG_N);
			set_flag_if(r->r[0] == 0,  FLAG_Z);
			clear_flag(FLAG_V);
			set_flag_if(tmp & 1,       FLAG_C);
        goto step_end;
		}

		case 00073: /* WRTLCK */ {
			if (r->model != DCJ11) {
				illegal_trap(r);
        goto step_end;
			}
			if ((op & 070) == 0) {
				illegal_trap(r);
        goto step_end;
			}
			DECODE_DST();
			word tmp = r->r[0];
			PUT_WORD(tmp);
			set_flag_if(tmp & SIGN, FLAG_N);
			set_flag_if(tmp == 0,  FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}

		case 00065: /* MFPD */ {
			if (r->model != DCJ11) {
				illegal_trap(r);
        goto step_end;
			}
			DECODE_DST();
			GET_WORD(tmp);
			pushw(tmp);
			set_flag_if(tmp & SIGN, FLAG_N);
			set_flag_if(tmp == 0,  FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}
		case 01065: /* MFPI */ {
			if (r->model != DCJ11) {
				illegal_trap(r);
        goto step_end;
			}
			DECODE_DST();
			GET_WORD(tmp);
			pushw(tmp);
			set_flag_if(tmp & SIGN, FLAG_N);
			set_flag_if(tmp == 0,  FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}
		case 00066: /* MTPI */ {
			if (r->model != DCJ11) {
				illegal_trap(r);
        goto step_end;
			}
			DECODE_DST();
			word tmp;
			pullw(tmp);
			PUT_WORD(tmp);
			set_flag_if(tmp & SIGN, FLAG_N);
			set_flag_if(tmp == 0,  FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}
		case 01066: /* MTPD */ {
			if (r->model != DCJ11) {
				illegal_trap(r);
        goto step_end;
			}
			DECODE_DST();
			word tmp;
			pullw(tmp);
			PUT_WORD(tmp);
			set_flag_if(tmp & SIGN, FLAG_N);
			set_flag_if(tmp == 0,  FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}
    }

    //
    // Double operand instructions - register and address
    //

#define RA_REG(r) byte r = (op >> 6) & 07;

    switch((op & 0177000) >> 9) {
		case 0004: /* JSR */ {
			RA_REG(reg);
			DECODE_DST();
			pushw(r->r[reg]);
			r->r[reg] = r->r[7];
			r->r[7] = dst_offset;
        goto step_end;
		}

		case 0070: /* MUL */ {
			if (r->model == K1801VM1) {
				illegal_trap(r);
        goto step_end;
			}
			union u_word data1;
			union u_word data2;
			union u_dword tmp;
			RA_REG(reg);
			data1.u = r->r[reg];
			DECODE_DST();
			data2.u = get_data_word(r, dst_type, dst_offset);

			tmp.s = ((sdword) data1.s) * ((sdword) data2.s);

			r->r[reg] = tmp.u >> 16;
			r->r[reg | 1] = tmp.u & 0177777;

			set_flag_if(tmp.u == 0, FLAG_Z);
			set_flag_if(tmp.u & 0x80000000, FLAG_N);
			set_flag_if((tmp.s < -0100000) || (tmp.s >= 077777), FLAG_C);
			clear_flag(FLAG_V);
        goto step_end;
		}

		case 0071: /* DIV */ {
			if (r->model == K1801VM1) {
				illegal_trap(r);
        goto step_end;
			}
			union u_word data1;
			union u_word data2;
			union u_dword tmp;
			RA_REG(reg);
			data1.u = (r->r[reg] << 16) | r->r[reg | 1];
			DECODE_DST();
			data2.u = get_data_word(r, dst_type, dst_offset);
			if (data2.u == 0) {
				set_flag(FLAG_C);
				set_flag(FLAG_V);
				clear_flag(FLAG_Z);
				clear_flag(FLAG_N);
        goto step_end;
			}
			clear_flag(FLAG_C);

			tmp.s = data1.s / data2.s;

			set_flag_if(tmp.u == 0, FLAG_Z);
			set_flag_if(tmp.s < 0, FLAG_N);
			if ((tmp.s < -0100000) || (tmp.s >= 077777)) {
				clear_flag(FLAG_C);
				set_flag(FLAG_V);
				clear_flag(FLAG_Z);
				clear_flag(FLAG_N);
        goto step_end;
			}

			r->r[reg | 1] = (data1.s % data2.s) & 0177777;
			r->r[reg] = tmp.u & 0177777;

        goto step_end;
		}

		case 0072: /* ASH */ {
			if (r->model == K1801VM1) {
				illegal_trap(r);
        goto step_end;
			}
			RA_REG(reg);
			word tmp = r->r[reg];
			word old = tmp;
			DECODE_DST();
			GET_WORD(shift);

			if ((shift & 077) != 0) {
				if (shift & 040) {
					word count = 0100 - (shift & 077);
					while (count--) {
						set_flag_if(tmp & 1, FLAG_C);
						if (tmp & SIGN) {
							tmp = (tmp >> 1) | SIGN;
						} else {
							tmp >>= 1;
						}
					}
				} else {
					word count = shift & 037;
					while(count--) {
						set_flag_if(tmp & SIGN, FLAG_C);
						tmp <<= 1;
					}
				}
				set_flag_if((old & SIGN) != (tmp & SIGN), FLAG_V);
				r->r[reg] = tmp;
			} else {
				clear_flag(FLAG_V);
			}
			set_flag_if(tmp & SIGN, FLAG_N);
			set_flag_if(tmp == 0,   FLAG_Z);

        goto step_end;
		}

		case 0073: /* ASHC */ {
			if (r->model == K1801VM1) {
				illegal_trap(r);
        goto step_end;
			}
			RA_REG(reg);
			dword tmp = (r->r[reg] << 16) | r->r[reg | 1];
			dword old = tmp;
			DECODE_DST();
			GET_WORD(shift);

			if ((shift & 077) != 0) {
				if (shift & 040) {
					word count = 0100 - (shift & 077);
					while(count--) {
						set_flag_if(tmp & 1, FLAG_C);
						if (tmp & 0x80000000) {
							tmp = (tmp >> 1) | 0x80000000;
						} else {
							tmp >>= 1;
						}
					}
				} else {
					word count = shift & 037;
					while(count--) {
						set_flag_if(tmp & 0x80000000, FLAG_C);
						tmp <<= 1;
					}
				}
				set_flag_if((old & 0x80000000) != (tmp & 0x80000000), FLAG_V);
				r->r[reg] = tmp >> 16;
				r->r[reg | 1] = tmp & 0177777;
			} else {
				clear_flag(FLAG_V);
			}

			set_flag_if(tmp & 0x80000000, FLAG_N);
			set_flag_if(tmp == 0, FLAG_Z);

        goto step_end;
		}

		case 0074: /* XOR */ {
			RA_REG(reg);
			DECODE_DST();
			GET_WORD(tmp);
			tmp ^= r->r[reg];
			PUT_WORD(tmp);
			set_flag_if(tmp & SIGN,   FLAG_N);
			set_flag_if(tmp == 0,     FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}
    }

    //
    // Double operand instructions
    //

#define DECODE_SRC()  do { src_type = decode_data(r, (op >> 6) & 077, TYPE_WORD, &src_offset); if (src_type == TYPE_ERROR) return 0; } while (0)
#define DECODE_SRCB() do { src_type = decode_data(r, (op >> 6) & 077, TYPE_BYTE, &src_offset); if (src_type == TYPE_ERROR) return 0; } while (0)

#define GET_SWORD(a) word a = get_data_word(r, src_type, src_offset); if (r->fAbort) return 0;
#define GET_SBYTE(a) word a = get_data_byte(r, src_type, src_offset); if (r->fAbort) return 0;

    switch((op & 0170000) >> 12) {
		case 001: /* MOV */	{
			DECODE_SRC();
			GET_SWORD(tmp);
			DECODE_DST();
			PUT_WORD(tmp);
			set_flag_if(tmp & SIGN,   FLAG_N);
			set_flag_if(tmp == 0,     FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}
		case 011: /* MOVB */ {
			DECODE_SRCB();
			GET_SBYTE(tmp);
			DECODE_DSTB();
			PUT_BYTE_MOVB(tmp);
			set_flag_if(tmp & SIGN_B, FLAG_N);
			set_flag_if(tmp == 0,     FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}

		case 002: /* CMP */ {
			DECODE_SRC();
			GET_SWORD(tmp);
			DECODE_DST();
			GET_WORD(tmp1);
			word tmp2 = ~tmp1;
			dword tmp3 = ((dword) tmp) + ((dword) tmp2) + 1;
			tmp2 = tmp3 & 0177777;
			set_flag_if(tmp2 & SIGN,  FLAG_N);
			set_flag_if(tmp2 == 0,    FLAG_Z);
			set_flag_if(((tmp & SIGN) != (tmp1 & SIGN)) && ((tmp1 & SIGN) == (tmp2 & SIGN)), FLAG_V);
			set_flag_if(!(tmp3 & CARRY), FLAG_C);
        goto step_end;
		}
		case 012: /* CMPB */ {
			DECODE_SRCB();
			GET_SBYTE(tmp);
			DECODE_DSTB();
			GET_BYTE(tmp1);
			byte tmp2 = ~tmp1;
			dword tmp3 = ((dword) tmp) + ((dword) tmp2) + 1;
			tmp2 = tmp3 & 0377;
			set_flag_if(tmp2 & SIGN_B,  FLAG_N);
			set_flag_if(tmp2 == 0,      FLAG_Z);
			set_flag_if(((tmp & SIGN_B) != (tmp1 & SIGN_B)) && ((tmp1 & SIGN_B) == (tmp2 & SIGN_B)), FLAG_V);
			set_flag_if(!(tmp3 & CARRY_B), FLAG_C);
        goto step_end;
		}

		case 006: /* ADD */ {
			DECODE_SRC();
			GET_SWORD(tmp);
			DECODE_DST();
			GET_WORD(tmp1);
			dword tmp3 = ((dword) tmp) + ((dword) tmp1);
			word tmp2 = tmp3 & 0177777;
			PUT_WORD(tmp2);
			set_flag_if(tmp2 & SIGN,  FLAG_N);
			set_flag_if(tmp2 == 0,    FLAG_Z);
			set_flag_if(((tmp & SIGN) == (tmp1 & SIGN)) && ((tmp & SIGN) != (tmp2 & SIGN)), FLAG_V);
			set_flag_if(tmp3 & CARRY, FLAG_C);
        goto step_end;
		}

		case 016: /* SUB */ {
			DECODE_SRC();
			GET_SWORD(tmp);
			DECODE_DST();
			GET_WORD(tmp1);
			word tmp2 = ~tmp;
			dword tmp3 = ((dword) tmp1) + ((dword) tmp2) + 1;
			tmp2 = tmp3 & 0177777;
			PUT_WORD(tmp2);
			set_flag_if(tmp2 & SIGN,  FLAG_N);
			set_flag_if(tmp2 == 0,    FLAG_Z);
			set_flag_if(((tmp & SIGN) != (tmp1 & SIGN)) && ((tmp & SIGN) == (tmp2 & SIGN)), FLAG_V);
			set_flag_if(!(tmp3 & CARRY), FLAG_C);
        goto step_end;
		}

		case 003: /* BIT */ {
			DECODE_SRC();
			GET_SWORD(tmp);
			DECODE_DST();
			GET_WORD(tmp1);
			word tmp2 = tmp & tmp1;
			set_flag_if(tmp2 & SIGN,   FLAG_N);
			set_flag_if(tmp2 == 0,     FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}
		case 013: /* BITB */ {
			DECODE_SRCB();
			GET_SBYTE(tmp);
			DECODE_DSTB();
			GET_BYTE(tmp1);
			byte tmp2 = tmp & tmp1;
			set_flag_if(tmp2 & SIGN_B, FLAG_N);
			set_flag_if(tmp2 == 0,     FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}

		case 004: /* BIC */ {
			DECODE_SRC();
			GET_SWORD(tmp);
			DECODE_DST();
			GET_WORD(tmp1);
			tmp1 = (~tmp) & tmp1;
			PUT_WORD(tmp1);
			set_flag_if(tmp1 & SIGN,   FLAG_N);
			set_flag_if(tmp1 == 0,     FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}
		case 014: /* BICB */ {
			DECODE_SRCB();
			GET_SBYTE(tmp);
			DECODE_DSTB();
			GET_BYTE(tmp1);
			tmp1 = (~tmp) & tmp1;
			PUT_BYTE(tmp1);
			set_flag_if(tmp1 & SIGN_B, FLAG_N);
			set_flag_if(tmp1 == 0,     FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}

		case 005: /* BIS */ {
			DECODE_SRC();
			GET_SWORD(tmp);
			DECODE_DST();
			GET_WORD(tmp1);
			tmp1 = tmp | tmp1;
			PUT_WORD(tmp1);
			set_flag_if(tmp1 & SIGN,   FLAG_N);
			set_flag_if(tmp1 == 0,     FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}
		case 015: /* BISB */ {
			DECODE_SRCB();
			GET_SBYTE(tmp);
			DECODE_DSTB();
			GET_BYTE(tmp1);
			tmp1 = tmp | tmp1;
			PUT_BYTE(tmp1);
			set_flag_if(tmp1 & SIGN_B, FLAG_N);
			set_flag_if(tmp1 == 0,     FLAG_Z);
			clear_flag(FLAG_V);
        goto step_end;
		}
    }

	illegal_trap(r);
        goto step_end;
step_end:
    if ((r->model == K1801VM1 || r->model == K1801VM1G) && (r->TVE_CSR & 000020)) {
        if (r->TVE_COUNT != 0) {
            r->TVE_COUNT--;
        }
        if (r->TVE_COUNT == 0) {
            if (r->TVE_CSR & 000004) {
                if (r->TVE_PENDING) {
                    r->TVE_CSR |= 000200;
                } else {
                    r->TVE_PENDING = 1;
                }
            }
            if (!(r->TVE_CSR & 000002)) {
                if (r->TVE_CSR & 000010) {
                    r->TVE_CSR &= ~000020;
                }
                if (r->TVE_LIMIT == 0) {
                    r->TVE_COUNT = 0177777;
                } else {
                    r->TVE_COUNT = r->TVE_LIMIT;
                }
            }
        }
    }
    if (!skip_trace && (psw_before & FLAG_T)) {
        do_trace = 1;
    }
    if (defer_halt && r->fHaltSignal) {
        r->fHaltSignal = 0;
        if (is_vm2(r) && flag_is_set(FLAG_H)) {
            r->fWait = 0;
            return 0;
        }
        r->fWait = 0;
        handle_halt(r);
        return 0;
    }
    if (do_trace) {
        pushw(r->psw);
        pushw(r->r[7]);
        r->r[7] = load_word(r, 014);
        r->psw  = load_word(r, 016);
    }
    if (!do_trace) {
        if (r->model == K1801VM1G && r->TVE_PENDING && (r->TVE_CSR & 000004)) {
            if ((r->psw & 01000) == 0 && (r->psw & FLAG_P) == 0) {
                r->TVE_PENDING = 0;
                pushw(r->psw);
                pushw(r->r[7]);
                r->r[7] = load_word(r, 000270);
                r->psw  = load_word(r, 000272);
                return 0;
            }
        }
        if (!skip_irq) {
            if (r->poll_irq && r->poll_irq(r, &irq_vector)) {
                word vec;
                if (irq_accept(r, irq_vector, &vec)) {
                    pushw(r->psw);
                    pushw(r->r[7]);
                    r->r[7] = load_word(r, vec);
                    r->psw  = load_word(r, (word)(vec + 2));
                }
            }
        }
    }
    if (is_vm2(r)) {
    	if ((r->psw & (FLAG_P | FLAG_H)) != (FLAG_P | FLAG_H)) {
    		r->cps = r->psw;
    		r->cpc = r->r[7];
    	}
    }
    return 0;
}
