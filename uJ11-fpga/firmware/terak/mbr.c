/* Bounded MBR/extended-partition reader and read-only FAT16 config reader.
 * No runtime FAT accesses or writes to partition/configuration metadata. */
#include "terak.h"

#define PARTS 12
struct partition {
	u32 start, blocks;
	uint8_t type;
};
#ifdef UJ11_TERAK_BIOS
static struct partition partitions[PARTS] TERAK_COLD;
static unsigned profile_number;
static u32 card_capacity;
#else
static struct partition partitions[PARTS];
#endif
static uint8_t selected[4], formats[4], readonly[4];

static unsigned half(unsigned p)
{
	return sector[p] | (unsigned)sector[p + 1] << 8;
}

static int extended(unsigned type)
{
	return type == 5 || type == 15 || type == 133;
}

static int entry(unsigned p, struct partition *out, u32 base, u32 limit)
{
	u32 start = word(p + 8), blocks = word(p + 12);
	unsigned type = sector[p + 4];
	if (!type && !start && !blocks) {
		out->type = 0;
		return 1;
	}
	if ((sector[p] != 0 && sector[p] != 128) || !type || type == 238 || !start || !blocks ||
	                start > limit || base > limit - start || blocks > limit - base - start) {
		return 0;
	}
	out->start = base + start;
	out->blocks = blocks;
	out->type = type;
	return 1;
}

static int table(u32 capacity)
{
	if (!fetch(0) || half(510) != 0xaa55) {
		return 0;
	}
	for (unsigned i = 0; i < PARTS; i++) {
		partitions[i].type = 0;
	}
	for (unsigned i = 0; i < 4; i++) {
		struct partition *p = partitions + i;
		if (!entry(446 + i * 16, p, 0, capacity) || (p->type && extended(p->type) && i != 3)) {
			return 0;
		}
		for (unsigned j = 0; p->type && j < i; j++) {
			struct partition *q = partitions + j;
			if (q->type && p->start < q->start + q->blocks && q->start < p->start + p->blocks) {
				return 0;
			}
		}
	}
	if (partitions[0].type != 6 && partitions[0].type != 14) {
		return 0;
	}
	struct partition *container = partitions + 3;
	if (!extended(container->type)) {
		return 1;
	}
	u32 base = container->start, end = base + container->blocks, current = base;
	for (unsigned i = 4; i < PARTS; i++) {
		struct partition link, *p = partitions + i;
		if (current >= end || !fetch(current) || half(510) != 0xaa55 ||
		                !entry(446, p, current, end) || !p->type || extended(p->type) ||
		                !entry(462, &link, base, end)) {
			return 0;
		}
		for (unsigned b = 478; b < 510; b++) {
			if (sector[b]) {
				return 0;
			}
		}
		if (!link.type) {
			return 1;
		}
		if (!extended(link.type) || link.start < p->start + p->blocks) {
			return 0;
		}
		current = link.start;
	}
	return 0;
}

static int match(unsigned *p, const char *text, unsigned end)
{
	unsigned at = *p;
	while (*text) {
		unsigned c = at < end ? sector[at++] : 0;
		if (c >= 'A' && c <= 'Z') {
			c += 'a' - 'A';
		}
		if (c != (unsigned char)*text++) {
			return 0;
		}
	}
	*p = at;
	return 1;
}

static void spaces(unsigned *p, unsigned end)
{
	while (*p < end && (sector[*p] == ' ' || sector[*p] == '\t')) {
		++*p;
	}
}

static int token(unsigned *p, unsigned c, unsigned end)
{
	spaces(p, end);
	if (*p >= end || sector[*p] != c) {
		return 0;
	}
	++*p;
	spaces(p, end);
	return 1;
}

static int config(unsigned size)
{
	unsigned p = 0, version = 0, drives = 0;
	static const char *const names[] = {"sssd", "dssd", "ssdd", "dsdd", "terak-ssdd", "terak-dsdd"};
	for (unsigned i = 0; i < 4; i++) {
		selected[i] = 0;
	}
	while (p < size) {
		unsigned end = p;
		while (end < size && sector[end] != '\r' && sector[end] != '\n' && sector[end] != '#') {
			++end;
		}
		unsigned next = end;
		while (next < size && sector[next] != '\r' && sector[next] != '\n') {
			++next;
		}
		while (next < size && (sector[next] == '\r' || sector[next] == '\n')) {
			++next;
		}
		spaces(&p, end);
		if (p != end) {
			if (match(&p, "version", end)) {
				if (version || !token(&p, '=', end) || !match(&p, "1", end)) {
					return 0;
				}
				version = 1;
			} else {
				if (!match(&p, "drive", end) || p == end || sector[p] < '0' || sector[p] > '3') {
					return 0;
				}
				unsigned u = sector[p++] - '0';
				if ((drives & (1u << u)) || !token(&p, '=', end)) {
					return 0;
				}
				drives |= 1u << u;
				if (!match(&p, "off", end)) {
					unsigned number = 0;
					while (p < end && sector[p] >= '0' && sector[p] <= '9') {
						number = number * 10 + sector[p++] - '0';
						if (number > PARTS) {
							return 0;
						}
					}
					if (number < 2 || !token(&p, ',', end)) {
						return 0;
					}
					selected[u] = number;
					formats[u] = MEDIA_FD;
					if (!match(&p, "fd", end)) {
						if (!match(&p, "raw", end) || !token(&p, ',', end)) {
							return 0;
						}
						unsigned f;
						for (f = 0; f < 6; f++) {
							if (match(&p, names[f], end)) {
								break;
							}
						}
						if (f == 6) {
							return 0;
						}
						formats[u] = f + 1;
					}
					if (!token(&p, ',', end)) {
						return 0;
					}
					readonly[u] = match(&p, "ro", end);
					if (!readonly[u] && !match(&p, "rw", end)) {
						return 0;
					}
				}
			}
			spaces(&p, end);
			if (p != end) {
				return 0;
			}
		}
		p = next;
	}
	return version && drives;
}

/* A <=512-byte config occupies one FAT cluster even when fragmented on disk.
 * Validate its allocation in every FAT copy; only the first data sector is read. */
static int fat_config(void)
{
#ifdef UJ11_TERAK_BIOS
	char name[] = "TERAK   CFG";
	if (profile_number) {
		name[5] = '0' + profile_number;
	}
	int found = fat_open(name, partitions[0].start, partitions[0].blocks, 512);
	if (found != 1) {
		return profile_number ? -1 : found;
	}
	int size = fat_read();
	return size > 0 && config(size) ? 1 : -1;
#else
	struct partition *boot = partitions;
	if (!fetch(boot->start) || half(510) != 0xaa55 || half(11) != 512) {
		return -1;
	}
	unsigned spc = sector[13], reserved = half(14), copies = sector[16], entries = half(17), spf = half(22);
	u32 total = half(19) ? half(19) : word(32);
	u32 root = reserved + copies * spf, data = root + (entries + 15) / 16;
	if (!spc || spc > 128 || (spc & (spc - 1)) || !reserved || !copies || copies > 2 ||
	                !entries || !spf || data >= total || total > boot->blocks) {
		return -1;
	}
	u32 clusters = (total - data) / spc;
	if (clusters < 4085 || clusters >= 65525 || (clusters + 2) * 2 > spf * 512u) {
		return -1;
	}
	unsigned cluster = 0, size = 0;
	for (unsigned i = 0; i < entries; i++) {
		if (!(i & 15) && !fetch(boot->start + root + i / 16)) {
			return -1;
		}
		unsigned p = (i & 15) * 32;
		if (!sector[p]) {
			break;
		}
		if (sector[p] == 229 || (sector[p + 11] & 8)) {
			continue;
		}
		static const char name[] = "TERAK   CFG";
		unsigned j;
		for (j = 0; j < 11 && sector[p + j] == (unsigned char)name[j]; j++) {
		}
		if (j == 11) {
			if (cluster || (sector[p + 11] & 16) || half(p + 20) || !word(p + 28) || word(p + 28) > 512) {
				return -1;
			}
			cluster = half(p + 26);
			size = word(p + 28);
			if (cluster < 2 || cluster >= clusters + 2) {
				return -1;
			}
		}
	}
	if (!cluster) {
		return 0;
	}
	for (unsigned i = 0; i < copies; i++) {
		if (!fetch(boot->start + reserved + i * spf + cluster / 256) || half((cluster & 255) * 2) < 0xfff8) {
			return -1;
		}
	}
	if (!fetch(boot->start + data + (cluster - 2) * spc)) {
		return -1;
	}
	return config(size) ? 1 : -1;
#endif
}

int mbr_mount(u32 capacity)
{
#ifdef UJ11_TERAK_BIOS
	card_capacity = capacity;
#endif
	if (!table(capacity)) {
		return 0;
	}
	int configured = fat_config();
	if (configured < 0) {
		return 0;
	}
	unsigned mounted = 0;
	if (!configured) {
		for (unsigned i = 1; i < PARTS && mounted < 4; i++) {
			struct partition *p = partitions + i;
			if (p->type == 218 && media_open(mounted, p->start, p->blocks, MEDIA_FD, 1)) {
				++mounted;
			}
		}
	} else {
		for (unsigned u = 0; u < 4; u++) {
			if (!selected[u]) {
				continue;
			}
			struct partition *p = partitions + selected[u] - 1;
			for (unsigned v = 0; v < u; v++) {
				if (selected[u] == selected[v]) {
					return 0;
				}
			}
			if (p->type != 218 || !media_open(u, p->start, p->blocks, formats[u], readonly[u])) {
				return 0;
			}
			++mounted;
		}
	}
	return mounted != 0;
}

#ifdef UJ11_TERAK_BIOS
int mbr_select(unsigned profile)
{
	if (profile < 1 || profile > 4) {
		return 0;
	}
	struct terak_media saved[4];
	for (unsigned i = 0; i < 4; i++) {
		saved[i] = media[i];
		media[i].start = 0;
	}
	profile_number = profile;
	int ok = mbr_mount(card_capacity) && media[0].start;
	if (!ok) {
		for (unsigned i = 0; i < 4; i++) {
			media[i] = saved[i];
		}
	}
	return ok;
}

int mbr_bios(void)
{
	return partitions[0].type && fat_open("TBIOS   BIN", partitions[0].start, partitions[0].blocks, 4112) == 1;
}
#endif
