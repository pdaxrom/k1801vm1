/* uJ11 primary BIOS service; this port is absent from direct-boot profiles.
 * The BIOS runs on the guest CPU and uses the native display and keyboard. */
#include "terak.h"

static unsigned menu_active TERAK_COLD, selecting TERAK_COLD;
static unsigned selection_status TERAK_COLD, bios_command TERAK_COLD;
#define QX_BOOT MMIO(0x4000080cu)
#define BIOS_BUFFER 040000u

static int settings_io(unsigned slot, unsigned writing)
{
	u32 lba = mbr_settings() ? fat_setting(slot, writing) : 0;
	if (!lba) {
		return 0;
	}
	DMA_ADDR = BIOS_BUFFER;
	DMA_MODE = 1;
	DMA_COUNT = 256;
	WINDOW = 256u << 8;
	if (writing) {
		if (transfer(LOAD) & 2) {
			return 0;
		}
		u32 checksum = media_crc(sector) ^ word(508);
		return store(lba) && fetch(lba) && (media_crc(sector) ^ word(508)) == checksum;
	}
	return fetch(lba) && !(transfer(STORE) & 2);
}

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
			unsigned value = request >> 16, op = value >> 8, arg = value & 255;
			selecting = 1;
			BUS_CONTROL = 9;
			bios_command = op;
			if (op <= 1) {
				if (mbr_select(arg)) {
					selection_status = 0;
					if (!op) {
						publish_media(0);
						menu_active = 0;
					}
				}
			} else if (op == 2) {
				selection_status = mbr_summary(arg);
			} else if ((op == 3 || op == 4) && arg < 2) {
				selection_status = !settings_io(arg, op == 4);
			} else if (op == 6) {
				selection_status = QX_BOOT >> 31;
				QX_BOOT |= 1u << 31;
			}
			BUS_CONTROL = 1;
			selecting = 0;
		}
	}
	BUS_RESPONSE = menu_active && bios_command == 5 ? TIME : selection_status;
	return 1;
}
