/* Exercise the actual BIOS mount/FAT code with a bounded read-only card. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../firmware/terak/terak.h"

static uint8_t buffer[512], *card;
static u32 blocks;
volatile uint8_t *const sector = buffer;
#ifdef UJ11_TERAK_RL
struct terak_media rl_media[4];
#endif

int fetch(u32 lba)
{
	assert(lba < blocks);
	for (unsigned i = 0; i < 512; i++) {
		buffer[i] = card[lba * 512u + i];
	}
	return 1;
}

int store(u32 lba)
{
	(void)lba;
	assert(0 && "BIOS must never write card metadata");
	return 0;
}

int main(int argc, char **argv)
{
	assert(argc == 3);
	FILE *f = fopen(argv[1], "rb");
	assert(f && !fseek(f, 0, SEEK_END));
	long bytes = ftell(f);
	assert(bytes > 0 && bytes % 512 == 0);
	blocks = bytes / 512;
	card = malloc(bytes);
	assert(card && !fseek(f, 0, SEEK_SET) && fread(card, 1, bytes, f) == (size_t)bytes);
	fclose(f);
	int mounted = media_mount(blocks), selected = mbr_select(atoi(argv[2]));
	printf("{\"mounted\":%d,\"selected\":%d,\"drives\":[", mounted, selected);
	for (unsigned i = 0; i < TERAK_UNITS; i++) {
		struct terak_media *m = media + (i & 3);
#ifdef UJ11_TERAK_RL
		if (i >= 4) {
			m = rl_media + i - 4;
		}
#endif
		printf("%s%u", i ? "," : "", m->start);
	}
	printf("],\"bios\":\"");
	int valid = mbr_bios(), count = 0;
	if (valid) {
		while ((count = fat_read()) > 0) {
			for (int i = 0; i < count; i++) {
				printf("%02x", sector[i]);
			}
		}
	}
	printf("\",\"valid\":%d}\n", valid && !count);
	free(card);
	return 0;
}
