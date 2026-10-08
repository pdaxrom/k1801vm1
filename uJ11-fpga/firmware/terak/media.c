/* Media addressing is independent of the card's UJ11SD/MBR partition table.
 * Raw images contain C,H,R ascending payload only; synthesize their IDs. */
#include "terak.h"

#ifdef UJ11_TERAK_RL
struct terak_media media[4] TERAK_COLD;
#else
struct terak_media media[4];
#endif
uint8_t track_meta[TERAK_TRACK_META_BYTES];
#ifndef UJ11_TERAK_RL
static struct sd_label sd_label;
#endif

u32 word(unsigned offset)
{
	return sector[offset] | ((u32)sector[offset + 1] << 8) | ((u32)sector[offset + 2] << 16) |
	       ((u32)sector[offset + 3] << 24);
}

u32 media_crc(const volatile uint8_t *data)
{
	u32 crc = 0xffffffffu;
	for (unsigned i = 0; i < 508; i++) {
		crc ^= data[i];
		for (unsigned b = 0; b < 8; b++) {
			crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
		}
	}
	return ~crc;
}

static void geometry(unsigned format, unsigned cylinder, unsigned head,
                     unsigned *count, unsigned *n, unsigned *mfm)
{
	*count = format <= RAW_DSSD ? 26 : 15;
	*n = format <= RAW_DSSD ? 0 : 2;
	*mfm = format > RAW_DSSD;
	if (format >= RAW_TERAK_SSDD && !cylinder) {
		*count = 26;
		*n = head ? 1 : 0;
		*mfm = head != 0;
	}
}

u32 track_data(unsigned unit, unsigned cylinder, unsigned head)
{
	struct terak_media *m = media + unit;
	unsigned track = cylinder * m->heads + head;
	if (m->format == MEDIA_FD) {
		return (2 + track * 33) * 512;
	}
	if (m->format >= RAW_TERAK_SSDD) {
		if (!cylinder) {
			return head * 3328;
		}
		return (m->heads == 2 ? 9984 : 3328) + (track - m->heads) * 7680;
	}
	return track * (m->format <= RAW_DSSD ? 3328 : 7680);
}

int media_open(unsigned unit, u32 start, u32 blocks, unsigned format, unsigned readonly)
{
#ifdef UJ11_TERAK_RL
	if (format == RAW_RL01 || format == RAW_RL02) {
		unsigned size = format == RAW_RL01 ? 10240 : 20480;
		if (unit < 4 || unit >= 8 || !start || blocks < size) {
			return 0;
		}
		rl_media[unit - 4] = (struct terak_media) {
			start, size, format - RAW_RL01 + 1, !!readonly, format
		};
		return 1;
	}
#endif
	if (unit >= 4 || !start || !blocks || format > RAW_TERAK_DSDD) {
		return 0;
	}
	unsigned heads;
	u32 bytes;
	if (format == MEDIA_FD) {
		if (!fetch(start) || word(0) != 0x31314a55 || word(4) != 0x00314446 ||
		                word(8) != 1 || word(12) != 77 || word(20) != 33 || media_crc(sector) != word(508)) {
			return 0;
		}
		heads = word(16);
		if (heads < 1 || heads > 2 || word(24) != 1 + 77 * heads * 33) {
			return 0;
		}
		bytes = word(24) * 512;
	} else {
		heads = format == RAW_DSSD || format == RAW_DSDD || format == RAW_TERAK_DSDD ? 2 : 1;
		bytes = format <= RAW_DSSD ? 77 * heads * 3328 : 77 * heads * 7680;
		if (format >= RAW_TERAK_SSDD) {
			bytes = 76 * heads * 7680 + (heads == 2 ? 9984 : 3328);
		}
	}
	if ((bytes + 511) / 512 > blocks) {
		return 0;
	}
	media[unit].start = start;
	media[unit].blocks = blocks;
	media[unit].heads = heads;
	media[unit].readonly = !!readonly;
	media[unit].format = format;
	return 1;
}

int track_load(unsigned unit, unsigned cylinder, unsigned head)
{
	struct terak_media *m = media + unit;
	if (!m->start || cylinder >= 77 || head >= m->heads) {
		return 0;
	}
	if (m->format == MEDIA_FD) {
		if (!fetch(m->start + track_data(unit, cylinder, head) / 512 - 1) || word(0) != 0x314b5254 ||
		                !sector[4] || sector[4] > 32 || sector[5] > 6 || sector[6] > 1 ||
		                ((u32)sector[4] << sector[5]) > 128 || media_crc(sector) != word(508)) {
			return 0;
		}
		for (unsigned i = 0; i < sizeof(track_meta); i++) {
			track_meta[i] = sector[i];
		}
	} else {
		unsigned count, n, mfm;
		geometry(m->format, cylinder, head, &count, &n, &mfm);
		/* Only live CHRN/status entries are consumed for raw media. Avoid
		 * clearing unused padding on every polled QX READ ID. */
		track_meta[4] = count;
		track_meta[5] = n;
		track_meta[6] = mfm;
		for (unsigned i = 0; i < count; i++) {
			unsigned p = 32 + i * 8;
			track_meta[p] = cylinder;
			track_meta[p + 1] = head;
			track_meta[p + 2] = i + 1;
			track_meta[p + 3] = n;
			track_meta[p + 4] = 1;
		}
	}
	return 1;
}

int track_format(unsigned unit, unsigned cylinder, unsigned head)
{
	struct terak_media *m = media + unit;
	if (!m->start || m->readonly || cylinder >= 77 || head >= m->heads) {
		return 0;
	}
	if (m->format == MEDIA_FD) {
		return 1;
	}
	unsigned count, n, mfm;
	geometry(m->format, cylinder, head, &count, &n, &mfm);
	if (track_meta[4] != count || track_meta[5] != n || track_meta[6] != mfm) {
		return 0;
	}
	for (unsigned i = 0; i < count; i++) {
		unsigned p = 32 + i * 8;
		if (track_meta[p] != cylinder || track_meta[p + 1] != head || track_meta[p + 2] != i + 1 ||
		                track_meta[p + 3] != n || track_meta[p + 4] != 1) {
			return 0;
		}
	}
	return 1;
}

int track_save(unsigned unit, unsigned cylinder, unsigned head)
{
	if (!track_format(unit, cylinder, head)) {
		return 0;
	}
	if (media[unit].format != MEDIA_FD) {
		return 1;
	}
#ifdef UJ11_TERAK_RL
	/* Keep the original reserved tail when updating CHRN/status entries. */
	if (!fetch(media[unit].start + track_data(unit, cylinder, head) / 512 - 1)) {
		return 0;
	}
#endif
	for (unsigned i = 0; i < sizeof(track_meta); i++) {
		sector[i] = track_meta[i];
	}
	u32 crc = media_crc(sector);
	for (unsigned i = 0; i < 4; i++) {
		sector[508 + i] = crc >> (i * 8);
	}
	return store(media[unit].start + track_data(unit, cylinder, head) / 512 - 1);
}

int media_mount(u32 capacity)
{
	int result = 0;
	for (unsigned i = 0; i < 4; i++) {
		media[i].start = 0;
#ifdef UJ11_TERAK_RL
		rl_media[i].start = 0;
#endif
	}
#ifdef UJ11_TERAK_RL
	/* The independent hard-disk build uses the shared MBR/FAT16 layout.
	 * Keep the legacy UJ11SD-label reader in the preserved floppy builds. */
	result = mbr_mount(capacity);
#else
	/* A newly written MBR may coexist with a stale UJ11SD backup at LBA 1.
	 * Its partition table takes precedence over that obsolete backup. */
	if (fetch(0) && sector[510] == 0x55 && sector[511] == 0xaa) {
		result = mbr_mount(capacity);
		goto complete;
	}
	for (unsigned copy = 0; copy < 2; copy++) {
		if (fetch(copy) && sd_label_decode((const uint8_t *)sector, capacity, &sd_label)) {
			for (unsigned i = 0; i < sd_label.count; i++) {
				struct sd_partition *p = sd_label.part + i;
				if (p->kind == 7 && media_open(p->unit, p->start, p->blocks, MEDIA_FD,
				                               p->flags & SD_READONLY)) {
					if (media[p->unit].heads == p->media - 8u) {
						result = 1;
					} else {
						media[p->unit].start = 0;
					}
				}
			}
			return result;
		}
	}
	result = mbr_mount(capacity);
complete:
#endif
	if (!result) {
		for (unsigned i = 0; i < 4; i++) {
			media[i].start = 0;
#ifdef UJ11_TERAK_RL
			rl_media[i].start = 0;
#endif
		}
	}
	return result;
}
