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
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        if (offset == 0177706) {
            return r->TVE_LIMIT & 0377;
        }
        if (offset == 0177707) {
            return (r->TVE_LIMIT >> 8) & 0377;
        }
        if (offset == 0177710) {
            return r->TVE_COUNT & 0377;
        }
        if (offset == 0177711) {
            return (r->TVE_COUNT >> 8) & 0377;
        }
        if (offset == 0177712) {
            return r->TVE_CSR & 0377;
        }
        if (offset == 0177713) {
            return 0377;
        }
    }
    if (offset == 0177716) {
        return r->SEL1 & 0377;
    }
    if (offset == 0177717) {
        return (r->SEL1 >> 8) & 0377;
    }
    if (offset == 0177714) {
        return r->SEL2 & 0377;
    }
    if (offset == 0177715) {
        return (r->SEL2 >> 8) & 0377;
    }
    return mem[offset];
}

static void hardware_store_byte(regs *r, word offset, byte value)
{
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        if (offset == 0177706) {
            r->TVE_LIMIT = (word)((r->TVE_LIMIT & 0177400) | (value & 0377));
            return;
        }
        if (offset == 0177707) {
            r->TVE_LIMIT = (word)(((value & 0377) << 8) | (r->TVE_LIMIT & 0377));
            return;
        }
        if (offset == 0177710 || offset == 0177711) {
            return;
        }
        if (offset == 0177712) {
            r->TVE_CSR = (word)(0177400 | (value & 0177));
            r->TVE_COUNT = r->TVE_LIMIT;
            return;
        }
        if (offset == 0177713) {
            return;
        }
    }
    if (offset == 0177716) {
        r->SEL1 = (word)((r->SEL1 & 0177400) | (value & 0377));
        return;
    }
    if (offset == 0177717) {
        r->SEL1 = (word)(((value & 0377) << 8) | (r->SEL1 & 0377));
        return;
    }
    if (offset == 0177714) {
        r->SEL2 = (word)((r->SEL2 & 0177400) | (value & 0377));
        return;
    }
    if (offset == 0177715) {
        r->SEL2 = (word)(((value & 0377) << 8) | (r->SEL2 & 0377));
        return;
    }
	mem[offset] = value;
}

static word hardware_load_word(regs *r, word offset)
{
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        if (offset == 0177706) {
            return r->TVE_LIMIT;
        }
        if (offset == 0177710) {
            return r->TVE_COUNT;
        }
        if (offset == 0177712) {
            return r->TVE_CSR;
        }
    }
    if (offset == 0177716) {
        return r->SEL1;
    }
    if (offset == 0177714) {
        return r->SEL2;
    }
    return r->load_byte(r, offset) | (r->load_byte(r, offset + 1) << 8);
}

static void hardware_store_word(regs *r, word offset, word value)
{
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        if (offset == 0177706) {
            r->TVE_LIMIT = value;
            return;
        }
        if (offset == 0177710) {
            return;
        }
        if (offset == 0177712) {
            r->TVE_CSR = (word)(0177400 | (value & 0177));
            r->TVE_COUNT = r->TVE_LIMIT;
            return;
        }
    }
    if (offset == 0177716) {
        r->SEL1 = value;
        return;
    }
    if (offset == 0177714) {
        r->SEL2 = value;
        return;
    }
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
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        r->TVE_CSR = 0177400;
        r->TVE_LIMIT = 0;
        r->TVE_COUNT = 0;
        r->TVE_PENDING = 0;
    }
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
	r->poll_irq		= NULL;
	r->ramptr		= hardware_ramptr;
}
