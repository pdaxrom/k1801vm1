/* Host checks exercise the actual SERV partition/config/media code with a
 * bounded SD sector backend. RTL tests separately cover SPI, DMA and CPU boot. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../firmware/terak/terak.h"

static uint8_t buffer[512], *card;
volatile uint8_t *const sector = buffer;
static u32 blocks;
static unsigned writes;

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
	assert(lba < blocks);
	unsigned allowed = 0;
	for (unsigned u = 0; u < 4; u++) {
		allowed |= media[u].start && !media[u].readonly && lba >= media[u].start &&
		           lba - media[u].start < media[u].blocks;
	}
	assert(allowed);
	++writes;
	for (unsigned i = 0; i < 512; i++) {
		card[lba * 512u + i] = buffer[i];
	}
	return 1;
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	FILE *f = fopen(argv[1], "rb");
	assert(f && !fseek(f, 0, SEEK_END));
	long bytes = ftell(f);
	assert(bytes > 0 && bytes % 512 == 0);
	blocks = bytes / 512;
	card = malloc(bytes);
	assert(card && !fseek(f, 0, SEEK_SET) && fread(card, 1, bytes, f) == (size_t)bytes);
	fclose(f);
	int mounted = media_mount(blocks);
	assert(!writes);
	printf("{\"mounted\":%d,\"drives\":[", mounted);
	for (unsigned u = 0; u < 4; u++) {
		struct terak_media *m = media + u;
		printf("%s{\"start\":%u,\"heads\":%u,\"readonly\":%u,\"format\":%u}",
		       u ? "," : "", m->start, m->heads, m->readonly, m->format);
		if (!m->start) {
			continue;
		}
		u32 cursor = 0;
		for (unsigned c = 0; c < 77; c++) {
			for (unsigned h = 0; h < m->heads; h++) {
				assert(track_load(u, c, h));
				if (m->format == MEDIA_FD) {
					assert(track_data(u, c, h) == (2 + (c * m->heads + h) * 33) * 512);
					continue;
				}
				unsigned count = m->format <= RAW_DSSD ? 26 : 15;
				unsigned n = m->format <= RAW_DSSD ? 0 : 2, mfm = n != 0;
				if (m->format >= RAW_TERAK_SSDD && !c) {
					count = 26;
					n = h;
					mfm = h != 0;
				}
				assert(track_data(u, c, h) == cursor);
				assert(track_meta[4] == count && track_meta[5] == n && track_meta[6] == mfm);
				for (unsigned r = 1; r <= count; r++) {
					unsigned p = 32 + (r - 1) * 8;
					assert(track_meta[p] == c && track_meta[p + 1] == h && track_meta[p + 2] == r &&
					       track_meta[p + 3] == n && track_meta[p + 4] == 1);
				}
				cursor += count * (128u << n);
				assert((cursor + 511) / 512 <= m->blocks);
				if (!m->readonly) {
					assert(track_format(u, c, h) && track_save(u, c, h));
					assert(!writes);
					track_meta[36] = 3;
					assert(!track_format(u, c, h) && !track_save(u, c, h));
					track_meta[36] = 1;
					track_meta[4]--;
					assert(!track_format(u, c, h));
				}
			}
		}
		assert(!track_load(u, 77, 0) && !track_load(u, 0, m->heads));
	}
	puts("]}");
	free(card);
	return 0;
}
