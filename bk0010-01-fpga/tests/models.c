#include <assert.h>
#include <stdio.h>
#include "bk0010-01/bk_model.h"

static void expect(bk_mapper *mapper, unsigned address, int writing, int valid, unsigned expected)
{
	uint32_t actual = 0;
	assert(bk_mapper_address(mapper, address, writing, &actual) == valid);
	if (valid) {
		assert(actual == expected);
	}
}

static void assertions(void)
{
	bk_mapper mapper;
	bk_mapper_reset(&mapper, BK_MODEL_FOCAL);
	expect(&mapper, 0120000, 0, 1, BK_ROM_FOCAL);
	expect(&mapper, 0140000, 0, 0, 0);
	expect(&mapper, 0157777, 0, 0, 0);
	expect(&mapper, 0160000, 0, 1, BK_ROM_TESTS);
	expect(&mapper, 0177577, 0, 1, BK_ROM_TESTS + 8063);
	expect(&mapper, 0177600, 0, 0, 0);
	expect(&mapper, 0100000, 1, 0, 0);
	bk_mapper_reset(&mapper, BK_MODEL_BASIC);
	expect(&mapper, 0140000, 0, 1, BK_ROM_BASIC10 + 8192);
	bk_mapper_reset(&mapper, BK_MODEL_11M_FDD);
	for (unsigned page = 0; page < 8; page++) {
		bk_mapper_system_write(&mapper, 04000 | (page << 12) | (page << 8), 3);
		expect(&mapper, 040000, 1, 1, bk_ram_page(page) * 0x4000);
		expect(&mapper, 0100000, 1, 1, bk_ram_page(page) * 0x4000);
	}
	unsigned retained = mapper.bank;
	bk_mapper_system_write(&mapper, 04001, 1);
	assert(mapper.bank == retained);
	bk_mapper_system_write(&mapper, 04001, 2);
	assert(mapper.bank == retained);
	bk_mapper_system_write(&mapper, 1, 3);
	assert(mapper.bank == retained);
	bk_mapper_system_write(&mapper, 04001, 3);
	expect(&mapper, 0100000, 0, 1, BK_ROM_BASE);
	expect(&mapper, 0100000, 1, 0, 0);
	bk_mapper_system_write(&mapper, 04002, 3);
	expect(&mapper, 0120000, 0, 1, BK_ROM_BASE + 0x6000);
	expect(&mapper, 0140000, 0, 1, BK_ROM_BOS);
	expect(&mapper, 0160000, 0, 1, BK_ROM_EXTERNAL);
	expect(&mapper, 0170000, 0, 0, 0);
	bk_mapper_reset(&mapper, BK_MODEL_A16M);
	expect(&mapper, 0120000, 1, 1, BK_A16M_RAM + 0x2000);
	expect(&mapper, 0140000, 1, 1, BK_A16M_RAM);
	expect(&mapper, 0170000, 0, 1, BK_ROM_A16M);
	bk_mapper_a16_write(&mapper, 6);
	bk_mapper_a16_write(&mapper, 0100);
	bk_mapper_a16_write(&mapper, 0);
	expect(&mapper, 0100000, 0, 1, BK_A16M_RAM);
	expect(&mapper, 0100000, 1, 0, 0);
	expect(&mapper, 0110000, 1, 1, BK_A16M_RAM + 0x1000);
	expect(&mapper, 0140000, 0, 0, 0);
	bk_mapper_a16_write(&mapper, 010);
	expect(&mapper, 0120000, 0, 1, BK_ROM_BASIC10);
	expect(&mapper, 0160000, 0, 1, BK_ROM_BASIC10 + 0x4000);
	expect(&mapper, 0100000, 1, 0, 0);
}

int main(void)
{
	bk_mapper mapper;
	FILE *output = fopen("build/map-vectors.txt", "w");
	const unsigned rom_select[] = {0, 1, 2, 010, 020, 3};
	assert(output);
	assertions();
	for (unsigned model = 0; model < 5; model++) {
		unsigned configurations = model >= 3 ? 64 * 6 : model == 0 ? 32 : 1;
		for (unsigned state = 0; state < configurations; state++) {
			bk_mapper_reset(&mapper, model);
			mapper.bank = ((state / 6 / 8) << 12) | (((state / 6) % 8) << 8) | rom_select[state % 6];
			mapper.a16_mode = ((state / 4) << 4) | ((state & 3) << 2);
			for (unsigned sample = 0; sample <= 16; sample++) {
				unsigned address = sample == 16 ? 0177600 : sample * 0x1000;
				for (unsigned edge = 0; edge < 2; edge++) {
					unsigned offset = address + (edge && sample != 16 ? 0xfff : 0);
					for (int writing = 0; writing < 2; writing++) {
						uint32_t physical = 0;
						int valid = bk_mapper_address(&mapper, offset, writing, &physical);
						fprintf(output, "%x %x %x %x %x %x %x\n", model, mapper.bank,
						        mapper.a16_mode, offset, writing, valid, physical);
					}
				}
			}
		}
	}
	assert(!fclose(output));
	puts("PASS model maps: ROM holes, all RAM banks, read-only windows, word lanes and A16 modes");
	return 0;
}
