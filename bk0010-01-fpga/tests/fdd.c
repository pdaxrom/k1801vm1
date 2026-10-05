#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bk0010-01/bk_fdd.h"
#include "bk0010-01/bk_roms.h"
#include "bk0010-01/bk_timing.h"

static uint8_t disks[2][BK_FDD_SECTORS][512];
static unsigned int reads, writes;
static const uint32_t bases[2] = {34816, 36864};

static int transfer(void *context, uint32_t lba, uint8_t data[512], int writing)
{
	(void)context;
	for (unsigned int drive = 0; drive < 2; drive++) {
		if (lba >= bases[drive] && lba - bases[drive] < BK_FDD_SECTORS) {
			if (writing) {
				assert(drive == 0);
				memcpy(disks[drive][lba - bases[drive]], data, 512);
				writes++;
			} else {
				memcpy(data, disks[drive][lba - bases[drive]], 512);
				reads++;
			}
			return 0;
		}
	}
	assert(!"Native disk ROM escaped its raw SD partition");
	return -1;
}

static int read_sector(void *context, uint32_t lba, uint8_t data[512])
{
	return transfer(context, lba, data, 0);
}

static int write_sector(void *context, uint32_t lba, const uint8_t data[512])
{
	return transfer(context, lba, (uint8_t *)data, 1);
}

static void execute(regs *cpu, unsigned int entry, unsigned int mhz)
{
	cpu->r[6] = 04000;
	cpu->store_word(cpu, cpu->r[6], 01200);
	cpu->r[7] = entry;
	cpu->psw = 0340;
	cpu->fAbort = cpu->fWait = cpu->fTrap = 0;
	bk_hw_set_tick_hz(mhz * 1000000);
	for (unsigned int count = 0; count < 2000000; count++) {
		assert(core_step(cpu) == 0);
		bk_hw_tick_n(bk_timing_cycles(cpu->ir));
		if (cpu->r[7] == 01200) {
			return;
		}
	}
	fprintf(stderr, "FDD timeout entry=%06o pc=%06o psw=%06o error=%06o\n",
	        entry, cpu->r[7], cpu->psw, cpu->load_word(cpu, 052));
	abort();
}

int main(int argc, char **argv)
{
	unsigned int model = argc == 2 && strcmp(argv[1], "a16m") == 0 ? BK_MODEL_A16M : BK_MODEL_11M_FDD;
	const bk_disk_partition partitions[4] = {{34816, 1600, 0}, {36864, 1600, 1}, {0}, {0}};
	assert(bk_fdd_configure(45000, 2048, 32768, partitions, read_sector, write_sector, NULL) == 0);
	for (unsigned int drive = 0; drive < 2; drive++) {
		for (unsigned int sector = 0; sector < BK_FDD_SECTORS; sector++) {
			for (unsigned int byte = 0; byte < 512; byte++) {
				disks[drive][sector][byte] = sector * 31 + byte * 13 + drive;
			}
		}
	}
	regs cpu = {0};
	bk_rom_set roms;
	byte *memory = calloc(1, bk_hw_required_memory_size());
	assert(memory && bk_hw_set_memory(memory, bk_hw_required_memory_size()) == 0);
	assert(bk_hw_select_model(model) == 0);
	cpu.model = K1801VM1;
	bk_hw_connect(&cpu);
	assert(core_init(&cpu) == 0 && bk_roms_load(&roms, model, "roms") == 0 && bk_roms_attach(&roms) == 0);
	core_reset(&cpu);
	cpu.r[3] = 02000;
	execute(&cpu, 0160010, 4);
	const unsigned int sectors[] = {0, 9, 10, 19, 20, 1599};
	const unsigned int frequencies[] = {3, 4, 6, 8, 12, 16, 24};
	for (unsigned int frequency = 0; frequency < 7; frequency++) {
		assert(bk_fdd_configure(45000, 2048, 32768, partitions, read_sector, write_sector, NULL) == 0);
		cpu.r[3] = 02000;
		execute(&cpu, 0160010, frequencies[frequency]);
		for (unsigned int drive = 0; drive < 2; drive++) {
			for (unsigned int sample = 0; sample < sizeof(sectors) / sizeof(sectors[0]); sample++) {
				unsigned int sector = sectors[sample];
				memory[02034] = drive;
				cpu.r[0] = sector;
				cpu.r[1] = 0400;
				cpu.r[2] = 01000;
				execute(&cpu, 0160004, frequencies[frequency]);
				if (cpu.psw & FLAG_C) {
					fprintf(stderr, "Read failed: sector=%u drive=%u mhz=%u error=%06o\n",
					        sector, drive, frequencies[frequency], cpu.load_word(&cpu, 052));
					abort();
				}
				assert(memcmp(memory + 01000, disks[drive][sector], 512) == 0);
			}
		}
	}
	for (unsigned int frequency = 0; frequency < 7; frequency++) {
		assert(bk_fdd_configure(45000, 2048, 32768, partitions, read_sector, write_sector, NULL) == 0);
		cpu.r[3] = 02000;
		execute(&cpu, 0160010, frequencies[frequency]);
		memory[02034] = 0;
		for (unsigned int byte = 0; byte < 512; byte++) {
			memory[01000 + byte] = byte ^ 0xa5;
		}
		cpu.r[0] = 10;
		cpu.r[1] = -0400;
		cpu.r[2] = 01000;
		execute(&cpu, 0160004, frequencies[frequency]);
		assert(!(cpu.psw & FLAG_C) && bk_fdd_flush() == 0);
		assert(memcmp(memory + 01000, disks[0][10], 512) == 0);
		unsigned int previous_writes = writes;
		memory[02034] = 1;
		cpu.r[0] = 10;
		cpu.r[1] = -0400;
		cpu.r[2] = 01000;
		execute(&cpu, 0160004, frequencies[frequency]);
		assert((cpu.psw & FLAG_C) && writes == previous_writes);
		memory[02034] = memory[02022] = memory[02032] = memory[02033] = 0;
		memory[02017] = 0x42;
		cpu.store_word(&cpu, 02064, 0400);
		execute(&cpu, 0160012, frequencies[frequency]);
		assert(!(cpu.psw & FLAG_C) && bk_fdd_flush() == 0);
		for (unsigned int sector = 0; sector < 10; sector++) {
			for (unsigned int byte = 0; byte < 512; byte++) {
				assert(disks[0][sector][byte] == 0x42);
			}
		}
	}
	assert(!bk_fdd_error());
	printf("PASS native %s: read CHS boundaries at seven frequencies, write, RO, format; raw SD reads=%u writes=%u\n",
	       model == BK_MODEL_A16M ? "A16M" : "DISK 327", reads, writes);
	core_fini(&cpu);
	bk_roms_free(&roms);
	free(memory);
	return 0;
}
