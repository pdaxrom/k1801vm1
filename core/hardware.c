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
#include <string.h>
#include "hardware.h"

static byte *mem = NULL;
static size_t mem_size = 0;

#if defined(ENABLE_MMU) && (ENABLE_MMU)
#define PHYS_MEM_SIZE (1u << 22)
#else
#define PHYS_MEM_SIZE (1u << 16)
#endif

size_t hwstub_required_memory_size(void)
{
	return PHYS_MEM_SIZE;
}

int hwstub_set_memory(byte *memory, size_t size)
{
	if (!memory || size < PHYS_MEM_SIZE) {
		mem = NULL;
		mem_size = 0;
		return -1;
	}
	mem = memory;
	mem_size = size;
	return 0;
}

void hwstub_clear_memory_binding(void)
{
	mem = NULL;
	mem_size = 0;
}

static int vm1_load_byte(regs *r, word offset, byte *value_out)
{
    if (r->model != K1801VM1 && r->model != K1801VM1G) {
        return 0;
    }
    switch (offset) {
    case 0177700:
        *value_out = 0377;
        return 1;
    case 0177701:
        *value_out = 0177;
        return 1;
    case 0177702:
        *value_out = r->VM1_RAP_PRESENT ? 0377 : 0;
        return 1;
    case 0177703:
        *value_out = r->VM1_RAP_PRESENT ? 0177 : 0;
        return 1;
    case 0177704:
        *value_out = 0340;
        return 1;
    case 0177705:
        *value_out = 0177;
        return 1;
    case 0177706:
        *value_out = (byte)(r->TVE_LIMIT & 0377);
        return 1;
    case 0177707:
        *value_out = (byte)((r->TVE_LIMIT >> 8) & 0377);
        return 1;
    case 0177710:
        *value_out = (byte)(r->TVE_COUNT & 0377);
        return 1;
    case 0177711:
        *value_out = (byte)((r->TVE_COUNT >> 8) & 0377);
        return 1;
    case 0177712:
        *value_out = (byte)(r->TVE_CSR & 0377);
        return 1;
    case 0177713:
        *value_out = 0377;
        return 1;
    default:
        return 0;
    }
}

static int vm1_store_byte(regs *r, word offset, byte value)
{
    if (r->model != K1801VM1 && r->model != K1801VM1G) {
        return 0;
    }
    switch (offset) {
    case 0177700:
    case 0177701:
        return 1;
    case 0177702:
    case 0177703:
        r->VM1_RAP_PRESENT = 0;
        return 1;
    case 0177704:
    case 0177705:
        return 1;
    case 0177706:
        r->TVE_LIMIT = (word)((r->TVE_LIMIT & 0177400) | (value & 0377));
        return 1;
    case 0177707:
        r->TVE_LIMIT = (word)(((word)value << 8) | (r->TVE_LIMIT & 0377));
        return 1;
    case 0177710:
    case 0177711:
        return 1;
    case 0177712:
        r->TVE_CSR = (word)(0177400 | (value & 0177));
        r->TVE_COUNT = r->TVE_LIMIT;
        return 1;
    case 0177713:
        return 1;
    default:
        return 0;
    }
}

static int vm1_load_word(regs *r, word offset, word *value_out)
{
    if (r->model != K1801VM1 && r->model != K1801VM1G) {
        return 0;
    }
    switch (offset) {
    case 0177700:
        *value_out = 017777;
        return 1;
    case 0177702:
        *value_out = r->VM1_RAP_PRESENT ? 017777 : 0;
        return 1;
    case 0177704:
        *value_out = 0177340;
        return 1;
    case 0177706:
        *value_out = r->TVE_LIMIT;
        return 1;
    case 0177710:
        *value_out = r->TVE_COUNT;
        return 1;
    case 0177712:
        *value_out = r->TVE_CSR;
        return 1;
    default:
        return 0;
    }
}

static int vm1_store_word(regs *r, word offset, word value)
{
    if (r->model != K1801VM1 && r->model != K1801VM1G) {
        return 0;
    }
    switch (offset) {
    case 0177700:
        return 1;
    case 0177702:
        r->VM1_RAP_PRESENT = 0;
        return 1;
    case 0177704:
        return 1;
    case 0177706:
        r->TVE_LIMIT = value;
        return 1;
    case 0177710:
        return 1;
    case 0177712:
        r->TVE_CSR = (word)(0177400 | (value & 0177));
        r->TVE_COUNT = r->TVE_LIMIT;
        return 1;
    default:
        return 0;
    }
}

int hwstub_vm1_load_byte(regs *r, word offset, byte *value_out)
{
    return vm1_load_byte(r, offset, value_out);
}

int hwstub_vm1_store_byte(regs *r, word offset, byte value)
{
    return vm1_store_byte(r, offset, value);
}

int hwstub_vm1_load_word(regs *r, word offset, word *value_out)
{
    return vm1_load_word(r, offset, value_out);
}

int hwstub_vm1_store_word(regs *r, word offset, word value)
{
    return vm1_store_word(r, offset, value);
}

static byte hardware_load_byte(regs *r, word offset)
{
	byte value;
	if (vm1_load_byte(r, offset, &value)) {
		return value;
    }
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        (void)value;
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
	if (!mem) {
		return 0;
	}
    return mem[offset];
}

static void hardware_store_byte(regs *r, word offset, byte value)
{
    if (vm1_store_byte(r, offset, value)) {
        return;
    }
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        (void)value;
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
	if (!mem) {
		return;
	}
	mem[offset] = value;
}

static word hardware_load_word(regs *r, word offset)
{
    word value;
    if (vm1_load_word(r, offset, &value)) {
        return value;
    }
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        (void)value;
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
    if (vm1_store_word(r, offset, value)) {
        return;
    }
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        (void)value;
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
	(void)r;
	if (!mem || mem_size < PHYS_MEM_SIZE) {
		return -1;
	}
	memset(mem, 0, PHYS_MEM_SIZE);

	return 0;
}

static void hardware_reset(regs *r)
{
    if (r->model == K1801VM1 || r->model == K1801VM1G) {
        r->TVE_CSR = 0177400;
        r->TVE_LIMIT = 0;
        r->TVE_COUNT = 0;
        r->TVE_PENDING = 0;
        r->VM1_RAP_PRESENT = 1;
    }
}

static void hardware_fini(regs *r)
{
	(void)r;
	mem = NULL;
	mem_size = 0;
}

static byte *hardware_ramptr(regs *r, word offset)
{
	(void)r;
	if (!mem) {
		return NULL;
	}
	return &mem[offset];
}

#if defined(ENABLE_MMU) && (ENABLE_MMU)
static byte hardware_load_byte_pa(regs *r, dword offset)
{
	(void)r;
	if (!mem) {
		return 0;
	}
	return mem[offset & (PHYS_MEM_SIZE - 1)];
}

static void hardware_store_byte_pa(regs *r, dword offset, byte value)
{
	(void)r;
	if (!mem) {
		return;
	}
	mem[offset & (PHYS_MEM_SIZE - 1)] = value;
}

static word hardware_load_word_pa(regs *r, dword offset)
{
	(void)r;
	if (!mem) {
		return 0;
	}
	byte lo = mem[offset & (PHYS_MEM_SIZE - 1)];
	byte hi = mem[(offset + 1) & (PHYS_MEM_SIZE - 1)];
	return (word)(lo | ((word)hi << 8));
}

static void hardware_store_word_pa(regs *r, dword offset, word value)
{
	(void)r;
	if (!mem) {
		return;
	}
	mem[offset & (PHYS_MEM_SIZE - 1)] = (byte)(value & 0377);
	mem[(offset + 1) & (PHYS_MEM_SIZE - 1)] = (byte)((value >> 8) & 0377);
}
#endif

void hwstub_connect(regs *r)
{
	r->load_byte	= hardware_load_byte;
	r->store_byte	= hardware_store_byte;
	r->load_word	= hardware_load_word;
	r->store_word	= hardware_store_word;
#if defined(ENABLE_MMU) && (ENABLE_MMU)
	r->load_byte_pa = hardware_load_byte_pa;
	r->store_byte_pa = hardware_store_byte_pa;
	r->load_word_pa = hardware_load_word_pa;
	r->store_word_pa = hardware_store_word_pa;
#else
	r->load_byte_pa = NULL;
	r->store_byte_pa = NULL;
	r->load_word_pa = NULL;
	r->store_word_pa = NULL;
#endif
	r->init			= hardware_init;
	r->reset		= hardware_reset;
	r->fini			= hardware_fini;
	r->poll_irq		= NULL;
	r->ramptr		= hardware_ramptr;
}
