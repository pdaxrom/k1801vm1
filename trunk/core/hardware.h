/*
 * mk90.h
 *
 *  Created on: 27.10.2016
 *      Author: sash
 *
 *  Access to peripheral devices
 *
 */

#ifndef MK90_H_
#define MK90_H_
#include "core.h"

#if 0

static byte hardware_load_byte(regs *r, word offset);
static void hardware_store_byte(regs *r, word offset, byte value);
static word hardware_load_word(regs *r, word offset);
static void hardware_store_word(regs *r, word offset, word value);

static int  hardware_init(regs *r);
static void hardware_reset(regs *r);
static void hardware_fini(regs *r);

static byte *hardware_ramptr(regs *r, word offset);
#endif

void hwstub_connect(regs *r);

#endif /* MK90_H_ */
