/*
 * mk90.c
 *
 *  Created on: 26.10.2016
 *      Author: sash
 */

#include <stdio.h>
#include <unistd.h>
#include <SDL.h>

#include "core/hardware.h"

enum {
	KA1835VG3 = 0,
	KA1835VG4,
	KA1835VG5,
	KA512VI1
};

typedef struct {
	char *name;
	word addr;
	word length;
	word mask;
	byte (*read_byte)	(byte addr);
	void (*write_byte)	(byte addr, byte value);
	word (*read_word)	(byte addr);
	void (*write_word)	(byte addr, word value);
} Io_Map;

static float scale_x = 1;
static float scale_y = 1;

static int exit_request = 0;

static byte *mem = NULL;
static byte ka1835vg3_reg[8];
static byte ka1835vg4_reg[8];

/*
 * KA1835VG3
 */

static byte ka1835vg3_read_byte(byte addr)
{
	return ka1835vg3_reg[addr];
}

static void ka1835vg3_write_byte(byte addr, byte value)
{
	ka1835vg3_reg[addr] = value;
}

static word ka1835vg3_read_word(byte addr)
{
	if (addr & 1) {
	    return ka1835vg3_reg[addr + 1];
	} else {
	    return (ka1835vg3_reg[addr + 1] << 8) | ka1835vg3_reg[addr];
	}
}

static void ka1835vg3_write_word(byte addr, word value)
{
	if (addr & 1) {
	    ka1835vg3_reg[addr + 1] = value >> 8;
	} else {
	    ka1835vg3_reg[addr    ] = value & 0xff;
	    ka1835vg3_reg[addr + 1] = value >> 8;
	}
}

/*
 * KA1835VG4
 */

static byte ka1835vg4_read_byte(byte addr)
{
	return ka1835vg4_reg[addr];
}

static void ka1835vg4_write_byte(byte addr, byte value)
{
	ka1835vg4_reg[addr] = value;
}

static word ka1835vg4_read_word(byte addr)
{
	return (ka1835vg4_reg[addr + 1] << 8) | ka1835vg4_reg[addr];
}

static void ka1835vg4_write_word(byte addr, word value)
{
	ka1835vg4_reg[addr    ] = value & 0xff;
	ka1835vg4_reg[addr + 1] = value >> 8;
}

/*
 * IO mapping
 */

static Io_Map *get_device(word addr)
{
	static Io_Map io_map[] = {
			{ "KA1835VG3", 0xe800, 0x8, 0x7, ka1835vg3_read_byte, ka1835vg3_write_byte, ka1835vg3_read_word, ka1835vg3_write_word },
			{ "KA1835VG4", 0xe810, 0x8, 0x7, ka1835vg4_read_byte, ka1835vg4_write_byte, ka1835vg4_read_word, ka1835vg4_write_word },
			{ "KA1835VG5", 0xe81a, 0x4, 0x3, NULL, NULL, NULL, NULL },
			{ "KA512VI1",  0xea00, 0x80,0x7f,NULL, NULL, NULL, NULL },
	};

	int i;

	for (i = 0; i < sizeof(io_map) / sizeof(Io_Map); i++) {
		if (addr >= io_map[i].addr && addr < io_map[i].addr + io_map[i].length) {
			return &io_map[i];
		}
	}

	return NULL;
}

static byte hardware_load_byte(regs *r, word offset)
{
	Io_Map *dev = get_device(offset);
	if (dev) {
		if (dev->read_byte) {
			byte value = dev->read_byte(offset & dev->mask);
			SDL_Log("%s: Read byte from [%04X] -> %02X\n", dev->name, offset, value);
			return value;
		}
	}

    return mem[offset];
}

static void hardware_store_byte(regs *r, word offset, byte value)
{
	Io_Map *dev = get_device(offset);
	if (dev) {
		if (dev->write_byte) {
			SDL_Log("%s: Write byte to [%04X] <- %02X\n", dev->name, offset, value);
			dev->write_byte(offset & dev->mask, value);
			return;
		}
	}

//	if (offset == 0177566) {
//	    printf("%c", value);
//	    fflush(stdout);
//	    return;
//	}

	//fixme: hack
//	if ((offset >= 0100000 && offset <= 0157777) || (offset >= 0166000 && offset <= 0176777)) {
//	    return;
//	}

	mem[offset] = value;
}

static word hardware_load_word(regs *r, word offset)
{
	Io_Map *dev = get_device(offset);
	if (dev) {
		if (dev->read_word) {
			word value = dev->read_word(offset & dev->mask);
//			SDL_Log("%s: Read word from [%04X] -> %02X\n", dev->name, offset, value);
			return value;
		}
	}

    return r->load_byte(r, offset) | (r->load_byte(r, offset + 1) << 8);
}

static void hardware_store_word(regs *r, word offset, word value)
{
	Io_Map *dev = get_device(offset);
	if (dev) {
		if (dev->write_word) {
//			SDL_Log("%s: Write word to [%04X] <- %02X\n", dev->name, offset, value);
			dev->write_word(offset & dev->mask, value);
			return;
		}
	}

	r->store_byte(r, offset,     value & 0377);
	r->store_byte(r, offset + 1, value >> 8);
}

static int hardware_init(regs *r)
{
	if (!mem) {
		mem = malloc(65536);
	}

    for (int i = 0; i < 65535; i++) {
        mem[i] = rand();
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

void mk90_connect(regs *r)
{
	r->ram_fast = NULL;
	r->ram_fast_size = 0;
	r->load_byte	= hardware_load_byte;
	r->store_byte	= hardware_store_byte;
	r->load_word	= hardware_load_word;
	r->store_word	= hardware_store_word;
	r->init			= hardware_init;
	r->reset		= hardware_reset;
	r->fini			= hardware_fini;
	r->ramptr		= hardware_ramptr;
}
