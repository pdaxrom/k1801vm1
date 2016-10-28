/*
 * hardware.c
 *
 *  Created on: 27.10.2016
 *      Author: sash
 *
 *  Empty stub for peripheral access
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include "hardware.h"

static byte *mem = NULL;

static byte hardware_load_byte(regs *r, word offset)
{
    return mem[offset];
}

static void hardware_store_byte(regs *r, word offset, byte value)
{
	mem[offset] = value;
}

static word hardware_load_word(regs *r, word offset)
{
    return r->load_byte(r, offset) | (r->load_byte(r, offset + 1) << 8);
}

static void hardware_store_word(regs *r, word offset, word value)
{
	r->store_byte(r, offset,     value & 0377);
	r->store_byte(r, offset + 1, value >> 8);
}

static int hardware_init(regs *r)
{
	if (!mem) {
		mem = malloc(65536);
	}

	return 0;
}

static void hardware_reset(regs *r)
{

}

static void hardware_fini(regs *r)
{
	if (mem) {
		free(mem);
		mem = NULL;
	}
}

static byte *hardware_ramptr(regs *r, word offset)
{
	return &mem[offset];
}

void hwstub_connect(regs *r)
{
	r->load_byte	= hardware_load_byte;
	r->store_byte	= hardware_store_byte;
	r->load_word	= hardware_load_word;
	r->store_word	= hardware_store_word;
	r->init			= hardware_init;
	r->reset		= hardware_reset;
	r->fini			= hardware_fini;
	r->ramptr		= hardware_ramptr;
}
