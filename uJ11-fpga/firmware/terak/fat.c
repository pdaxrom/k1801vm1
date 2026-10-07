/* Bounded read-only FAT16 loader for the optional primary BIOS.
 * Every cluster link is checked against both FATs; no metadata is written. */
#include "terak.h"

static struct {
	u32 start, data, fat, clusters, size, remaining;
	unsigned cluster, next, block, spc, copies, spf;
} file TERAK_COLD;

static unsigned half(unsigned p)
{
	return sector[p] | (unsigned)sector[p + 1] << 8;
}

int fat_open(const char *name, u32 start, u32 blocks, unsigned limit)
{
	if (!fetch(start) || half(510) != 0xaa55 || half(11) != 512) {
		return -1;
	}
	unsigned reserved = half(14), entries = half(17);
	file.spc = sector[13];
	file.copies = sector[16];
	file.spf = half(22);
	u32 total = half(19) ? half(19) : word(32);
	u32 root = reserved + file.copies * file.spf;
	file.data = root + (entries + 15) / 16;
	if (!file.spc || file.spc > 128 || (file.spc & (file.spc - 1)) || !reserved ||
	                !file.copies || file.copies > 2 || !entries || !file.spf || file.data >= total || total > blocks) {
		return -1;
	}
	file.clusters = (total - file.data) / file.spc;
	if (file.clusters < 4085 || file.clusters >= 65525 || (file.clusters + 2) * 2 > file.spf * 512u) {
		return -1;
	}
	file.start = start;
	file.fat = reserved;
	file.cluster = file.size = file.block = 0;
	for (unsigned i = 0; i < entries; i++) {
		if (!(i & 15) && !fetch(start + root + i / 16)) {
			return -1;
		}
		unsigned p = (i & 15) * 32;
		if (!sector[p]) {
			break;
		}
		if (sector[p] == 229 || (sector[p + 11] & 8)) {
			continue;
		}
		unsigned j;
		for (j = 0; j < 11 && sector[p + j] == (unsigned char)name[j]; j++) {
		}
		if (j == 11) {
			if (file.cluster || (sector[p + 11] & 16) || half(p + 20)) {
				return -1;
			}
			file.cluster = half(p + 26);
			file.size = word(p + 28);
			if (file.cluster < 2 || file.cluster >= 0xfff0 || file.cluster >= file.clusters + 2 ||
			                !file.size || file.size > limit) {
				return -1;
			}
		}
	}
	file.remaining = file.size;
	return file.cluster != 0;
}

/* Returns valid bytes in sector, zero at EOF, or -1 for an invalid chain/read. */
int fat_read(void)
{
	if (!file.remaining) {
		return 0;
	}
	if (!file.block) {
		for (unsigned i = 0; i < file.copies; i++) {
			if (!fetch(file.start + file.fat + i * file.spf + file.cluster / 256)) {
				return -1;
			}
			unsigned next = half((file.cluster & 255) * 2);
			if (i && next != file.next) {
				return -1;
			}
			file.next = next;
		}
		if (file.remaining <= file.spc * 512u ? file.next < 0xfff8 :
		                file.next < 2 || file.next >= 0xfff0 || file.next >= file.clusters + 2) {
			return -1;
		}
	}
	if (!fetch(file.start + file.data + (file.cluster - 2) * file.spc + file.block)) {
		return -1;
	}
	unsigned count = file.remaining < 512 ? file.remaining : 512;
	file.remaining -= count;
	if (++file.block == file.spc) {
		file.block = 0;
		file.cluster = file.next;
	}
	return count;
}
