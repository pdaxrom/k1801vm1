/*
 * mk90.h
 *
 *  Created on: 27.10.2016
 *      Author: sash
 *
 *  Access to peripheral devices
 *
 */

#ifndef _CORE_HARDWARE_H_
#define _CORE_HARDWARE_H_
#include "core.h"
#include <stddef.h>

void hwstub_connect(regs *r);
size_t hwstub_required_memory_size(void);
int hwstub_set_memory(byte *memory, size_t size);
void hwstub_clear_memory_binding(void);

#endif
