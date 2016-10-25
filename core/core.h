/*
 * core.h
 *
 *  Created on: 19.10.2016
 *      Author: Alexander Chukov <sash@pdaXrom.org>
 */

#ifndef CORE_H_
#define CORE_H_

#ifndef INLINE
#define INLINE inline
#endif

#ifndef byte
#define byte unsigned char
#endif
#ifndef word
#define word unsigned short
#endif
#ifndef sbyte
#define sbyte signed char
#endif
#ifndef sword
#define sword signed short
#endif

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
	unsigned short	u;
	signed short	s;
};

union u_long {
	unsigned long	u;
	signed long		s;
};

typedef struct _regs {
    word	psw, r[8];

    word	SEL1;

    word	fTrap;

    word	fWait;

    byte	*mem;
} regs;

void core_reset(regs *r);

int core_step(regs *r);


#endif
