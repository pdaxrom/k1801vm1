#include <stdio.h>
#include <stdlib.h>

#include "bk0010-01/bk_hw.h"
#include "bk0010-01/bk_timing.h"

static byte *read_rom(const char *path, size_t size)
{
	FILE *file = fopen(path, "rb");
	byte *data = malloc(size);

	if (!file || !data || fread(data, 1, size, file) != size) {
		fprintf(stderr, "Cannot read ROM: %s\n", path);
		exit(1);
	}
	fclose(file);
	return data;
}

static void run(regs *cpu, unsigned count)
{
	while (count--) {
		core_step(cpu);
	}
}

static void save_screen(const char *path, const byte *memory)
{
	FILE *file = fopen(path, "wb");

	if (!file || fwrite(memory + BK_VRAM_BASE, 1, BK_VRAM_SIZE, file) != BK_VRAM_SIZE || fclose(file)) {
		fprintf(stderr, "Cannot save VRAM: %s\n", path);
		exit(1);
	}
}

int main(void)
{
	regs cpu = {0};
	byte *memory = calloc(1, bk_hw_required_memory_size());
	byte *monitor = read_rom("roms/MONIT10.ROM", 8192);
	byte *basic = read_rom("roms/BASIC10.ROM", 24576);
	const char keys[] = "PRINT 2+2\012";

	cpu.model = K1801VM1;
	if (!memory || bk_hw_set_memory(memory, bk_hw_required_memory_size())) {
		return 1;
	}
	bk_hw_connect(&cpu);
	if (core_init(&cpu)) {
		return 1;
	}
	bk_hw_set_rom_segment(monitor, 0100000, 020000);
	bk_hw_set_rom_segment(basic, 0120000, 060000);
	core_reset(&cpu);
	run(&cpu, 200000);
	save_screen("build/reference-boot-vram.bin", memory);
	for (size_t i = 0; i < sizeof(keys) - 1; i++) {
		bk_hw_handle_key(keys[i]);
		run(&cpu, 20000);
	}
	run(&cpu, 200000);
	save_screen("build/reference-print-vram.bin", memory);
	printf("PASS reference C emulator: MONIT/BASIC, PRINT 2+2, PC=%06o PSW=%06o SHIFT=%06o\n",
	       cpu.r[7], cpu.psw, bk_hw_shift_reg());
	core_fini(&cpu);
	free(memory);
	free(monitor);
	free(basic);
	return 0;
}
