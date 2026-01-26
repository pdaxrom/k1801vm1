/*
 * core.h
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#ifndef CORE_H_
#define CORE_H_

#include <stdint.h>

#ifndef INLINE
#define INLINE inline
#endif

#ifndef byte
#define byte uint8_t
#endif
#ifndef word
#define word uint16_t
#endif
#ifndef dword
#define dword uint32_t
#endif

#ifndef sbyte
#define sbyte int8_t
#endif
#ifndef sword
#define sword int16_t
#endif
#ifndef sdword
#define sdword int32_t
#endif

enum {
	K1801VM1 = 0,
	K1801VM1G,
	K1801VM2,
	K1806VM2,
	DCJ11,
};

#define SET_BIT(a) (1 << a)

#define BIT_H 8
#define BIT_P 7
#define BIT_T 4
#define BIT_N 3
#define BIT_Z 2
#define BIT_V 1
#define BIT_C 0

#define FLAG_H SET_BIT(BIT_H)
#define FLAG_P SET_BIT(BIT_P)
#define FLAG_T SET_BIT(BIT_T)
#define FLAG_N SET_BIT(BIT_N)
#define FLAG_Z SET_BIT(BIT_Z)
#define FLAG_V SET_BIT(BIT_V)
#define FLAG_C SET_BIT(BIT_C)

union u_word {
	word	u;
	sword	s;
};

union u_dword {
	dword	u;
	sdword	s;
};

typedef struct _regs {
	byte	model;

    word	psw, r[8];
    word    ir;

    word	cps, cpc;	/* 0177676 0177674 */

    word	SEL0;		/* unaddressed SEL register */
    word	SEL1, SEL2;	/* 0177716 0177714 external registers */

    word	fTrap;

    word	fWait;
    word    fAbort;
    word    fHaltSignal;

    byte (* load_byte)	(struct _regs *r, word offset);
    void (* store_byte)	(struct _regs *r, word offset, byte value);
    word (* load_word)	(struct _regs *r, word offset);
    void (* store_word)	(struct _regs *r, word offset, word value);

    int  (* init)		(struct _regs *r);
    void (* reset)		(struct _regs *r);
    void (* fini)		(struct _regs *r);

    int  (* poll_irq)	(struct _regs *r, word *vector);

    byte *(* ramptr)	(struct _regs *r, word offset);
} regs;

void core_init(regs *r);
void core_reset(regs *r);
int  core_step (regs *r);
void core_fini (regs *r);

#endif
