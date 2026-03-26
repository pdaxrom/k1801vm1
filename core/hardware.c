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

#ifndef __not_in_flash_func
#define __not_in_flash_func(func_name) func_name
#endif

static byte *pending_mem = NULL;
static size_t pending_mem_size = 0;

#if defined(ENABLE_MMU) && (ENABLE_MMU)
#define PHYS_MEM_SIZE (1u << 22)
#else
#define PHYS_MEM_SIZE (1u << 16)
#endif

/*
 * The I/O page occupies 0160000..0177777 in the 16-bit address space.
 * The ram_fast fast path must not cover that range so that model-specific
 * register decode in core_load/store_*_ex fires for those addresses.
 */
#define RAM_FAST_LIMIT 0160000u

size_t hwstub_required_memory_size(void)
{
    return PHYS_MEM_SIZE;
}

int hwstub_set_memory(byte *memory, size_t size)
{
    if (!memory || size < PHYS_MEM_SIZE) {
        pending_mem = NULL;
        pending_mem_size = 0;
        return -1;
    }
    pending_mem = memory;
    pending_mem_size = size;
    return 0;
}

void hwstub_clear_memory_binding(void)
{
    pending_mem = NULL;
    pending_mem_size = 0;
}

static int hwstub_bind_memory(regs *r, byte *memory, size_t size)
{
    if (!memory || size < PHYS_MEM_SIZE) {
        r->hwstub_mem = NULL;
        r->hwstub_mem_size = 0;
        return -1;
    }
    r->hwstub_mem = memory;
    r->hwstub_mem_size = (uint32_t)size;
    return 0;
}

static byte hardware_load_byte(regs *r, word offset)
{
    return r->hwstub_mem[offset];
}

static void hardware_store_byte(regs *r, word offset, byte value)
{
    r->hwstub_mem[offset] = value;
}

static word hardware_load_word(regs *r, word offset)
{
    return (word)(r->hwstub_mem[offset] |
                  ((word)r->hwstub_mem[(word)(offset + 1)] << 8));
}

static void hardware_store_word(regs *r, word offset, word value)
{
    r->hwstub_mem[offset] = (byte)(value & 0377);
    r->hwstub_mem[(word)(offset + 1)] = (byte)(value >> 8);
}

static int hardware_init(regs *r)
{
    if (!r->hwstub_mem || r->hwstub_mem_size < PHYS_MEM_SIZE) {
        return -1;
    }
    memset(r->hwstub_mem, 0, PHYS_MEM_SIZE);

    return 0;
}

static void hardware_reset(regs *r)
{
    (void)r;
}

static void hardware_fini(regs *r)
{
    r->hwstub_mem = NULL;
    r->hwstub_mem_size = 0;
}

static byte *hardware_ramptr(regs *r, word offset)
{
    if (!r->hwstub_mem) {
        return NULL;
    }
    return &r->hwstub_mem[offset];
}

#if defined(ENABLE_MMU) && (ENABLE_MMU)
static byte hardware_load_byte_pa(regs *r, dword offset)
{
    if (!r->hwstub_mem) {
        return 0;
    }
    return r->hwstub_mem[offset & (PHYS_MEM_SIZE - 1)];
}

static void hardware_store_byte_pa(regs *r, dword offset, byte value)
{
    if (!r->hwstub_mem) {
        return;
    }
    r->hwstub_mem[offset & (PHYS_MEM_SIZE - 1)] = value;
}

static word hardware_load_word_pa(regs *r, dword offset)
{
    if (!r->hwstub_mem) {
        return 0;
    }
    byte lo = r->hwstub_mem[offset & (PHYS_MEM_SIZE - 1)];
    byte hi = r->hwstub_mem[(offset + 1) & (PHYS_MEM_SIZE - 1)];
    return (word)(lo | ((word)hi << 8));
}

static void hardware_store_word_pa(regs *r, dword offset, word value)
{
    if (!r->hwstub_mem) {
        return;
    }
    r->hwstub_mem[offset & (PHYS_MEM_SIZE - 1)] = (byte)(value & 0377);
    r->hwstub_mem[(offset + 1) & (PHYS_MEM_SIZE - 1)] =
        (byte)((value >> 8) & 0377);
}
#endif

void hwstub_connect(regs *r)
{
    r->ram_fast = NULL;
    r->ram_fast_size = RAM_FAST_LIMIT;
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
    if (!r->hwstub_mem && pending_mem) {
        (void)hwstub_bind_memory(r, pending_mem, pending_mem_size);
        pending_mem = NULL;
        pending_mem_size = 0;
    }
}
