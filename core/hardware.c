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

static byte hardware_load_byte(regs *r, word offset)
{
    (void)r;
    return mem[offset];
}

static void hardware_store_byte(regs *r, word offset, byte value)
{
    (void)r;
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
    (void)r;
    if (!mem || mem_size < PHYS_MEM_SIZE) {
        return -1;
    }
    memset(mem, 0, PHYS_MEM_SIZE);

    return 0;
}

static void hardware_reset(regs *r)
{
    (void)r;
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
