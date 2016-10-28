/*
 * mk90.h
 *
 *  Created on: 27.10.2016
 *      Author: sash
 */

#ifndef MK90_H_
#define MK90_H_
#include "core.h"

int hardware_load_byte(regs *r, word offset, byte *value);
int hardware_store_byte(regs *r, word offset, byte value);
int hardware_load_word(regs *r, word offset, word *value);
int hardware_store_word(regs *r, word offset, word value);

int start_hardware(regs *r, int width, int height);
void stop_hardware(regs *r);

#endif /* MK90_H_ */
