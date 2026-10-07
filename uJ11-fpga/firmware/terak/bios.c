/* uJ11 primary BIOS service; this port is absent from direct-boot profiles.
 * The BIOS runs on the guest CPU and uses the native display and keyboard. */
#include "terak.h"

static unsigned menu_active, selecting, selection_status;

/* Freestanding GCC may lower structure assignments to memcpy. */
__attribute__((used))
#ifndef __clang__
__attribute__((externally_visible))
#endif
void *memcpy(void *dest, const void *source, unsigned bytes)
{
	uint8_t *to = dest;
	const uint8_t *from = source;
	for (unsigned i = 0; i < bytes; i++) {
		to[i] = from[i];
	}
	return dest;
}

int bios_load(void)
{
	if (!mbr_bios()) {
		return 0;
	}
	int count = fat_read();
	if (count < 16 || word(0) != 0x42544a55 || word(12) != 1) {
		return 0;
	}
	unsigned bytes = word(4), checksum = word(8), sum = 0, address = 0, offset = 16;
	if (!bytes || (bytes & 1) || bytes > 4096 || checksum > 65535) {
		return 0;
	}
	BUS_CONTROL = 9;
	for (;;) {
		unsigned chunk = count - offset;
		if (!chunk || (chunk & 1) || chunk > bytes - address) {
			break;
		}
		for (unsigned p = offset; p < (unsigned)count; p += 2) {
			sum = (sum + sector[p] + ((unsigned)sector[p + 1] << 8)) & 65535;
		}
		DMA_ADDR = address;
		DMA_MODE = 1;
		DMA_COUNT = chunk / 2;
		WINDOW = (offset / 2) | ((unsigned)count / 2 << 8);
		if (transfer(STORE) & 2) {
			break;
		}
		address += chunk;
		count = fat_read();
		if (count <= 0) {
			BUS_CONTROL = 1;
			if (count || address != bytes || sum != checksum) {
				return 0;
			}
			menu_active = 1;
			selection_status = 1;
			publish_media(1);
			return 1;
		}
		offset = 0;
	}
	BUS_CONTROL = 1;
	return 0;
}

int bios_poll(void)
{
	if (selecting) {
		return 1;
	}
	if (!(BUS_STATUS & 1) || (BUS_REQUEST & 017776) != 011020) {
		return 0;
	}
	u32 request = BUS_REQUEST;
	if (request & (1u << 13)) {
		selection_status = 1;
		if (menu_active && (request & 0140000) == 0140000) {
			selecting = 1;
			BUS_CONTROL = 9;
			if (mbr_select(request >> 16)) {
				publish_media(0);
				menu_active = selection_status = 0;
			}
			BUS_CONTROL = 1;
			selecting = 0;
		}
	}
	BUS_RESPONSE = selection_status;
	return 1;
}
