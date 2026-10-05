#ifndef BK_ROMS_H
#define BK_ROMS_H

#include "bk_hw.h"

typedef struct {
	byte *segments[6];
	unsigned int count;
	unsigned int model;
} bk_rom_set;

int bk_roms_load(bk_rom_set *set, unsigned int model, const char *directory);
int bk_roms_attach(const bk_rom_set *set);
void bk_roms_free(bk_rom_set *set);

#endif
