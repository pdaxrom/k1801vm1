#ifndef BK_MODEL_H
#define BK_MODEL_H

#include <stdint.h>
#include "bk_model_layout.h"

enum bk_model {
	BK_MODEL_A16M = 0,
	BK_MODEL_FOCAL = 1,
	BK_MODEL_BASIC = 2,
	BK_MODEL_11M_FDD = 3,
	BK_MODEL_11M_MSTD = 4
};

typedef struct {
	uint8_t model;
	uint16_t bank;
	uint8_t a16_mode;
	uint8_t a16_phase;
} bk_mapper;

static inline unsigned bk_ram_page(unsigned code)
{
	static const uint8_t pages[8] = {1, 5, 2, 3, 4, 7, 0, 6};
	return pages[code & 7];
}

static inline void bk_mapper_reset(bk_mapper *mapper, unsigned model)
{
	mapper->model = model;
	mapper->bank = 0;
	mapper->a16_mode = 0160;
	mapper->a16_phase = 0;
}

/* The BK-0011M memory latch accepts full word writes with bit 11 set. */
static inline void bk_mapper_system_write(bk_mapper *mapper, uint16_t value, unsigned lanes)
{
	if (mapper->model >= BK_MODEL_11M_FDD && lanes == 3 && (value & 04000)) {
		mapper->bank = value;
	}
}

static inline void bk_mapper_a16_write(bk_mapper *mapper, uint16_t value)
{
	if (mapper->model != BK_MODEL_A16M) {
		return;
	}
	mapper->a16_mode = (mapper->a16_mode & 0160) | (value & 014);
	if (mapper->a16_phase) {
		mapper->a16_mode = value & 0174;
		mapper->a16_phase = (value & 6) == 6;
	} else if ((value & 6) == 6) {
		mapper->a16_phase = 1;
	}
}

/* Returns 1 for a responding RAM/ROM segment, 0 for a bus timeout.
 * physical addresses are bytes, matching the SD bundle contract. */
static inline int bk_mapper_address(const bk_mapper *mapper, uint16_t address,
                                    int writing, uint32_t *physical)
{
	unsigned model = mapper->model;
	unsigned bank = address >> 12;
	uint32_t location;
	int readonly = 0;

	if (model > BK_MODEL_11M_MSTD || address >= 0177600) {
		return 0;
	}
	if (model >= BK_MODEL_11M_FDD) {
		if (address < 040000) {
			location = address;
		} else if (address < 0100000) {
			location = bk_ram_page(mapper->bank >> 12) * 0x4000 + (address & 0x3fff);
		} else if (address < 0140000) {
			unsigned select = mapper->bank & 033;
			if (select) {
				if (select != 1 && select != 2) {
					return 0;
				}
				location = BK_ROM_BASE + (select == 2 ? 0x4000 : 0) + (address & 0x3fff);
				readonly = 1;
			} else {
				location = bk_ram_page(mapper->bank >> 8) * 0x4000 + (address & 0x3fff);
			}
		} else if (address < 0160000) {
			location = BK_ROM_BOS + (address & 0x1fff);
			readonly = 1;
		} else {
			if (model == BK_MODEL_11M_FDD && address >= 0170000) {
				return 0;
			}
			location = BK_ROM_EXTERNAL + (address & 0x1fff);
			readonly = 1;
		}
	} else if (address < 0100000) {
		location = address;
	} else if (model == BK_MODEL_A16M) {
		unsigned mode = mapper->a16_mode;
		unsigned map = mode & 0160;
		if (address >= 0120000 && (mode & 010)) {
			location = BK_ROM_BASIC10 + address - 0120000;
			readonly = 1;
		} else if (bank == 8 || bank == 9) {
			if (map == 0160 || map == 060 || map == 0140 || map == 040) {
				location = BK_ROM_BASE + address - 0100000;
				readonly = 1;
			} else {
				location = BK_A16M_RAM + (bank - 8) * 0x1000 + (address & 0xfff);
				readonly = bank == 8 && (map == 0100 || map == 020);
				if (map == 0) {
					return 0;
				}
			}
		} else if (bank == 10 || bank == 11) {
			location = BK_A16M_RAM + (bank - 8) * 0x1000 + (address & 0xfff);
			if (map == 0 || map == 020 || map == 040 || map == 0140) {
				return 0;
			}
		} else if (bank == 12 || bank == 13) {
			if (map != 0160 && map != 060 && map != 040 && map != 0) {
				return 0;
			}
			location = BK_A16M_RAM + (bank - 12) * 0x1000 + (address & 0xfff);
		} else {
			if (map == 040 || map == 0) {
				location = BK_A16M_RAM + 0x2000 + (bank - 14) * 0x1000 + (address & 0xfff);
				if (bank == 15 && (map == 040 || !writing)) {
					return 0;
				}
			} else {
				if (map == 020 || (bank == 15 && map != 0160)) {
					return 0;
				}
				location = BK_ROM_A16M + (address & 0xfff);
				readonly = 1;
			}
		}
	} else if (address < 0120000) {
		location = BK_ROM_BASE + address - 0100000;
		readonly = 1;
	} else if (model == BK_MODEL_FOCAL) {
		if (address < 0140000) {
			location = BK_ROM_FOCAL + address - 0120000;
		} else if (address >= 0160000) {
			location = BK_ROM_TESTS + address - 0160000;
		} else {
			return 0;
		}
		readonly = 1;
	} else {
		location = BK_ROM_BASIC10 + address - 0120000;
		readonly = 1;
	}
	if (writing && readonly) {
		return 0;
	}
	*physical = location;
	return 1;
}

#endif
