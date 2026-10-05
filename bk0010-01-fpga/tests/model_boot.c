#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "bk0010-01/bk_roms.h"
#include "bk0010-01/bk_timing.h"

static void run(regs *cpu, unsigned int model, unsigned int count)
{
	unsigned int elapsed = 0;
	unsigned int frame = (model >= BK_MODEL_11M_FDD ? 4000000 : 3000000) / 50;
	while (count--) {
		assert(core_step(cpu) == 0);
		unsigned int cycles = bk_timing_cycles(cpu->ir);
		bk_hw_tick_n(cycles);
		elapsed += cycles;
		if (elapsed >= frame) {
			elapsed -= frame;
			bk_hw_frame_tick();
		}
	}
}

static void save_screen(unsigned int model, const char *phase)
{
	char path[128];
	snprintf(path, sizeof(path), "build/model-%u-%s.ppm", model, phase);
	FILE *file = fopen(path, "wb");
	assert(file);
	fprintf(file, "P6\n512 256\n255\n");
	uint32_t pixels[512 * 256];
	bk_hw_render_pixels(pixels);
	for (unsigned int position = 0; position < 512 * 256; position++) {
		unsigned char pixel[3] = {pixels[position] >> 16, pixels[position] >> 8, pixels[position]};
		assert(fwrite(pixel, 1, 3, file) == 3);
	}
	assert(fclose(file) == 0);
}

int main(void)
{
	for (unsigned int model = 0; model < 5; model++) {
		regs cpu = {0};
		bk_rom_set roms;
		byte *memory = calloc(1, bk_hw_required_memory_size());
		assert(memory && bk_hw_set_memory(memory, bk_hw_required_memory_size()) == 0);
		assert(bk_hw_select_model(model) == 0);
		cpu.model = K1801VM1;
		bk_hw_connect(&cpu);
		assert(core_init(&cpu) == 0);
		assert(bk_roms_load(&roms, model, "roms") == 0);
		assert(bk_roms_attach(&roms) == 0);
		core_reset(&cpu);
		run(&cpu, model, model == BK_MODEL_A16M ? 5000000 : 1000000);
		if (model == BK_MODEL_A16M) {
			/* A16M must see the read-only BK10 keyboard port, not a BK11 video register. */
			assert(cpu.load_word(&cpu, 0500) == 060);
			assert(cpu.r[7] == 0165404 || cpu.r[7] == 0165412);
		}
		save_screen(model, "boot");
		char path[128];
		snprintf(path, sizeof(path), "build/model-%u-vram.bin", model);
		FILE *vram = fopen(path, "wb");
		assert(vram && fwrite(bk_hw_vram_ptr(), 1, bk_hw_vram_size(), vram) == bk_hw_vram_size());
		assert(fclose(vram) == 0);
		printf("Model %u: PC=%06o PSW=%06o bank=%06o video=%06o scroll=%06o\n", model,
		       cpu.r[7], cpu.psw, bk_hw_bank_register(), bk_hw_video_register(), bk_hw_shift_reg());
		printf("Keyboard status=%06o RAM 105=%03o IRQ=%u HALT=%u\n",
		       cpu.load_word(&cpu, 0177660), memory[0105], cpu.fTrap, cpu.fHaltSignal);
		if (model == BK_MODEL_BASIC || model == BK_MODEL_11M_MSTD) {
			const char command[] = "PRINT 2+2\012";
			for (unsigned int key = 0; key < sizeof(command) - 1; key++) {
				bk_hw_handle_key(command[key]);
				run(&cpu, model, 20000);
			}
			run(&cpu, model, 200000);
			save_screen(model, "print");
			snprintf(path, sizeof(path), "build/model-%u-print-vram.bin", model);
			vram = fopen(path, "wb");
			assert(vram && fwrite(bk_hw_vram_ptr(), 1, bk_hw_vram_size(), vram) == bk_hw_vram_size());
			assert(fclose(vram) == 0);
		}
		core_fini(&cpu);
		bk_roms_free(&roms);
		free(memory);
	}
	return 0;
}
