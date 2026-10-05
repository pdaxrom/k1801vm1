#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bk0010-01/bk_fdd.h"
#include "bk0010-01/bk_roms.h"
#include "bk0010-01/bk_timing.h"

static unsigned int reads;
static unsigned char sectors[BK_FDD_SECTORS * 2];
static uint32_t disk_base = 40960;
static uint32_t second_base;

static int read_sector(void *context, uint32_t lba, uint8_t data[512])
{
	FILE *file = context;
	unsigned int index;
	if (lba >= disk_base && lba < disk_base + BK_FDD_SECTORS) {
		index = lba - disk_base;
	} else {
		assert(second_base && lba >= second_base && lba < second_base + BK_FDD_SECTORS);
		index = BK_FDD_SECTORS + lba - second_base;
	}
	assert(fseek(file, (long)lba * 512, SEEK_SET) == 0);
	assert(fread(data, 1, 512, file) == 512);
	sectors[index] = 1;
	reads++;
	if (reads <= 20 || reads % 100 == 0) {
		printf("SD read %u: LBA=%u disk sector=%u\n", reads, lba, index);
	}
	return 0;
}

static void save_screen(const char *prefix)
{
	char path[256];
	uint32_t pixels[512 * 256];
	snprintf(path, sizeof(path), "%s.ppm", prefix);
	FILE *file = fopen(path, "wb");
	assert(file);
	fprintf(file, "P6\n512 256\n255\n");
	bk_hw_render_pixels(pixels);
	for (unsigned int i = 0; i < 512 * 256; i++) {
		unsigned char pixel[3] = {pixels[i] >> 16, pixels[i] >> 8, pixels[i]};
		assert(fwrite(pixel, 1, 3, file) == 3);
	}
	assert(fclose(file) == 0);
	snprintf(path, sizeof(path), "%s-vram.bin", prefix);
	file = fopen(path, "wb");
	assert(file && fwrite(bk_hw_vram_ptr(), 1, 16384, file) == 16384);
	assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
	assert(argc >= 4);
	unsigned int model = (unsigned int)strtoul(argv[2], NULL, 0);
	unsigned int count = argc > 4 ? (unsigned int)strtoul(argv[4], NULL, 0) : 20000000;
	unsigned int frequency = argc > 8 ? (unsigned int)strtoul(argv[8], NULL, 0) :
	                         model >= BK_MODEL_11M_FDD ? 4000000 : 3000000;
	unsigned int elapsed = 0;
	FILE *sd = fopen(argv[1], "rb");
	assert(sd);
	disk_base = argc > 6 ? (uint32_t)strtoul(argv[6], NULL, 0) : 40960;
	second_base = argc > 7 ? (uint32_t)strtoul(argv[7], NULL, 0) : 0;
	assert(fseek(sd, 0, SEEK_END) == 0);
	long capacity = ftell(sd);
	assert(capacity > 0 && capacity % 512 == 0);
	const bk_disk_partition disks[4] = {
		{disk_base, 1600, 1}, {second_base, second_base ? 1600 : 0, 1}, {0}, {0}
	};
	assert(bk_fdd_configure((uint32_t)(capacity / 512), 2048, 32768, disks, read_sector, NULL, sd) == 0);
	regs cpu = {0};
	bk_rom_set roms;
	byte *memory = calloc(1, bk_hw_required_memory_size());
	assert(memory && bk_hw_set_memory(memory, bk_hw_required_memory_size()) == 0);
	assert(bk_hw_select_model(model) == 0);
	cpu.model = K1801VM1;
	bk_hw_connect(&cpu);
	assert(core_init(&cpu) == 0 && bk_roms_load(&roms, model, "roms") == 0 && bk_roms_attach(&roms) == 0);
	bk_hw_set_tick_hz(frequency);
	core_reset(&cpu);
	for (unsigned int i = 0; i < count; i++) {
		assert(core_step(&cpu) == 0);
		unsigned int cycles = bk_timing_cycles(cpu.ir);
		bk_hw_tick_n(cycles);
		elapsed += cycles;
		if (elapsed >= frequency / 50) {
			elapsed -= frequency / 50;
			bk_hw_frame_tick();
		}
		if (argc > 5 && i >= 5000000 && (i - 5000000) % 100000 == 0) {
			unsigned int key = (i - 5000000) / 100000;
			if (argv[5][key]) {
				bk_hw_handle_key(argv[5][key] == '~' ? 012 : argv[5][key]);
			} else {
				argc = 5;
			}
		}
		if (i % 1000000 == 0) {
			printf("Model %u instructions=%u PC=%06o PSW=%06o bank=%06o video=%06o reads=%u error=%d\n",
			       model, i, cpu.r[7], cpu.psw, bk_hw_bank_register(), bk_hw_video_register(), reads, bk_fdd_error());
			fflush(stdout);
		}
	}
	save_screen(argv[3]);
	char memory_path[256];
	snprintf(memory_path, sizeof(memory_path), "%s-memory.bin", argv[3]);
	FILE *dump = fopen(memory_path, "wb");
	assert(dump && fwrite(memory, 1, bk_hw_required_memory_size(), dump) == bk_hw_required_memory_size());
	assert(fclose(dump) == 0);
	unsigned int unique = 0;
	for (unsigned int i = 0; i < BK_FDD_SECTORS * 2; i++) {
		unique += sectors[i];
	}
	printf("Disk OS run finished: model=%u PC=%06o bank=%06o reads=%u unique=%u controller_error=%d\n",
	       model, cpu.r[7], bk_hw_bank_register(), reads, unique, bk_fdd_error());
	assert(!bk_fdd_error() && reads >= 100 && unique >= 100);
	char report_path[256];
	snprintf(report_path, sizeof(report_path), "%s.json", argv[3]);
	FILE *report = fopen(report_path, "w");
	assert(report);
	fprintf(report, "{\"model\":%u,\"frequency_hz\":%u,\"reads\":%u,\"unique_sectors\":%u,"
	                "\"pc\":%u,\"bank\":%u,\"video\":%u,\"scroll\":%u,\"controller_error\":false}\n",
	        model, frequency, reads, unique, cpu.r[7], bk_hw_bank_register(),
	        bk_hw_video_register(), bk_hw_shift_reg());
	assert(fclose(report) == 0);
	core_fini(&cpu);
	bk_roms_free(&roms);
	free(memory);
	assert(fclose(sd) == 0);
	return 0;
}
