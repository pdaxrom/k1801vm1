#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bk_roms.h"
#include "bk_rom_contract.h"

void bk_roms_free(bk_rom_set *set)
{
	for (unsigned int i = 0; i < set->count; i++) {
		free(set->segments[i]);
	}
	memset(set, 0, sizeof(*set));
}

int bk_roms_load(bk_rom_set *set, unsigned int model, const char *directory)
{
	if (!set || !directory || model > BK_MODEL_11M_MSTD) {
		return -1;
	}
	memset(set, 0, sizeof(*set));
	set->model = model;
	for (unsigned int i = 0; i < bk_rom_counts[model]; i++) {
		const bk_rom_spec *spec = &bk_rom_specs[model][i];
		char path[1024];
		int length = snprintf(path, sizeof(path), "%s/%s", directory, spec->name);
		if (length < 0 || (size_t)length >= sizeof(path)) {
			bk_roms_free(set);
			return -1;
		}
		FILE *file = fopen(path, "rb");
		byte *data = malloc(spec->size);
		if (!file || !data) {
			fprintf(stderr, "Cannot load ROM: %s\n", path);
			if (file) {
				fclose(file);
			}
			free(data);
			bk_roms_free(set);
			return -1;
		}
		int valid = fread(data, 1, spec->size, file) == spec->size && fgetc(file) == EOF;
		if (fclose(file)) {
			valid = 0;
		}
		if (valid && model == BK_MODEL_A16M && i == 2 && (data[4095] != 0372)) {
			valid = 0;
		}
		if (!valid) {
			fprintf(stderr, "Invalid ROM size or model signature: %s\n", path);
			free(data);
			bk_roms_free(set);
			return -1;
		}
		set->segments[set->count++] = data;
	}
	return 0;
}

int bk_roms_attach(const bk_rom_set *set)
{
	if (!set || set->model > BK_MODEL_11M_MSTD || set->count != bk_rom_counts[set->model]) {
		return -1;
	}
	for (unsigned int i = 0; i < set->count; i++) {
		const bk_rom_spec *spec = &bk_rom_specs[set->model][i];
		if (bk_hw_load_rom_segment(set->segments[i], spec->physical, spec->size)) {
			return -1;
		}
	}
	return 0;
}
