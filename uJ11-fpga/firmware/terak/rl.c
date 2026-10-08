/* RLV11: four registers at 174400, vector 160, 18-bit Q-bus DMA.
 * The 2 MiB profile uses RLV12's fifth register and 22-bit DMA.
 * DEC EK-RLV11-TD-001 chapter 3 / EK-RLV12-UG-002 section 4.6.
 * The command/sector algorithms follow
 * uJ11's existing SERV RL implementation; no expanded floppy geometry.
 * RL01/RL02 raw images contain 40 x 256-byte sectors per track, two heads.
 */
#include "terak.h"

struct terak_media rl_media[4] TERAK_COLD;
unsigned rl_irq TERAK_COLD;
#ifdef UJ11_TERAK_RLV12
#define RL_REGISTERS 5
#define RL_ADDRESS_MASK 0x3fffff
#else
#define RL_REGISTERS 4
#define RL_ADDRESS_MASK 0x3ffff
#endif
static uint16_t registers[RL_REGISTERS] TERAK_COLD, position[4] TERAK_COLD, header_crc TERAK_COLD;
static uint8_t rotation[4] TERAK_COLD, header_index TERAK_COLD;
static unsigned busy TERAK_COLD, epoch TERAK_COLD, generation TERAK_COLD;

__attribute__((noinline)) void rl_reset(void)
{
	for (unsigned i = 0; i < RL_REGISTERS; i++) {
		registers[i] = 0;
	}
	registers[0] = 0200;
	busy = rl_irq = header_index = 0;
	++epoch;
	/* INIT clears the controller, not the drive's cylinder/head. */
}

int rl_poll(u32 request)
{
	unsigned address = request & 017776;
#ifdef UJ11_TERAK_RLV12
	if (address < 014400 || address > 014410) {
#else
	if ((address & ~7u) != 014400) {
#endif
		return 0;
	}
	unsigned index = (address >> 1) & (RL_REGISTERS == 5 ? 7 : 3), lanes = (request >> 14) & 3;
	unsigned cs = registers[0], unit = (cs >> 8) & 3;
	unsigned value = registers[index];
	if (!index) {
		value = (cs & ~1u) | (rl_media[unit].start && !busy);
	}
	if (request & (1u << 13)) {
		unsigned mask = (lanes & 1 ? 255u : 0) | (lanes & 2 ? 65280u : 0);
		unsigned merged = (value & ~mask) | ((request >> 16) & mask);
		if (!index) {
			if (!(merged & 0100)) {
				rl_irq = 0;
			} else if (!busy && (cs & 0200) && !(cs & 0100)) {
				rl_irq = 1;
			}
			if (!busy) {
				registers[0] = (merged & 01776) | 0200;
#ifdef UJ11_TERAK_RLV12
				registers[4] = (registers[4] & ~3u) | ((merged >> 4) & 3);
#endif
				if ((lanes & 1) && !(merged & 0200)) {
					registers[0] &= ~0200u;
					busy = 1;
					rl_irq = header_index = 0;
					++epoch;
				}
			}
		} else if (!busy) {
			registers[index] = merged & (index == 1 ? 0177776 : 0177777);
#ifdef UJ11_TERAK_RLV12
			if (index == 4) {
				registers[4] &= 077;
				registers[0] = (cs & ~060u) | ((registers[4] & 3) << 4);
			}
#endif
			header_index = 0;
		}
	} else if (index == 3 && (lanes & 2) && header_index) {
		registers[3] = header_index == 1 ? 0 : header_crc;
		header_index = (header_index + 1) & 3;
	}
	BUS_RESPONSE = value;
	return 1;
}

static unsigned error(unsigned code)
{
	return 0100000u | (code << 10);
}

static int valid(void)
{
	return busy && epoch == generation;
}

static unsigned command(void)
{
	unsigned cs = registers[0], fn = (cs >> 1) & 7, unit = (cs >> 8) & 3, da = registers[2];
	struct terak_media *m = rl_media + unit;
	unsigned pos = position[unit], max_cylinder = m->heads == 2 ? 511 : 255;
	if (!fn) {
		return 0;
	}
	if (!m->start) {
		return error(5);
	}
	if (fn == 2) {
		if ((da & 7) != 3) {
			return error(1);
		}
		registers[3] = 035 | (pos & 64 ? 0100 : 0) | (m->heads == 2 ? 0200 : 0) |
		               (m->readonly ? 020000 : 0);
		return 0;
	}
	if (fn == 3) {
		if ((da & 3) != 1) {
			return error(1);
		}
		unsigned cylinder = pos >> 7, distance = da >> 7;
		if (da & 4) {
			cylinder += distance;
			if (cylinder > max_cylinder) {
				cylinder = max_cylinder;
			}
		} else {
			cylinder = distance > cylinder ? 0 : cylinder - distance;
		}
		position[unit] = (cylinder << 7) | (da & 020 ? 64 : 0);
		return 0;
	}
	if (fn == 4) {
		unsigned header = pos | rotation[unit], crc = 0, data = header;
		for (unsigned i = 0; i < 32; i++) {
			unsigned bit = (crc ^ data) & 1;
			crc = (crc >> 1) ^ (bit ? 0120001 : 0);
			data >>= 1;
		}
		registers[3] = header;
		header_crc = crc;
		header_index = 1;
		rotation[unit] = (rotation[unit] + 1) % 40;
		return 0;
	}
	if (fn == 5 && m->readonly) {
		return error(1) | 040000;
	}
	if (fn == 7) {
		da = pos | rotation[unit];
	} else if ((da >> 7) != (pos >> 7)) {
		return error(5);
	}
	unsigned sec = da & 63, cylinder = da >> 7, head = (da >> 6) & 1;
	if (sec >= 40 || cylinder > max_cylinder) {
		return error(5);
	}
	unsigned left = (-registers[3]) & 65535, maximum = (40 - sec) * 128, overrun = left > maximum;
	if (overrun) {
		left = maximum;
	}
#ifdef UJ11_TERAK_RLV12
	u32 address = registers[1] | ((u32)registers[4] << 16);
#else
	u32 address = registers[1] | ((cs & 060) << 12);
#endif
	unsigned original = registers[2], done = 0;
	while (left && valid()) {
		unsigned offset = sec & 1 ? 128 : 0, span = offset ? 128 : 256;
		unsigned count = left < span ? left : span;
		if (count <= 128) {
			span = 128;
		}
		u32 lba = m->start + (((cylinder * 2 + head) * 40 + sec) >> 1);
		/* Each RL sector occupies one half of an SD sector. Fetch before
		 * partial writes so LOAD's padding cannot erase its neighbour. */
		if (!fetch(lba)) {
			return error(4);
		}
		if (!valid()) {
			return 0;
		}
		DMA_ADDR = address;
		/* The sector engine selects 18- or 22-bit address arithmetic here. */
		DMA_MODE = RL_REGISTERS == 5;
		DMA_COUNT = count;
		WINDOW = offset | ((offset + span) << 8);
		u32 status = transfer(fn == 5 ? LOAD : fn == 1 ? COMPARE : STORE);
		if (!valid()) {
			return 0;
		}
		if (status & 2) {
			return error(status & 8 ? 8 : 4);
		}
		if (status & 4) {
			return error(2);
		}
		if (fn == 5 && !store(lba)) {
			return error(4);
		}
		if (!valid()) {
			return 0;
		}
		left -= count;
		done += count;
		address = (address + 2 * count) & RL_ADDRESS_MASK;
		sec += (count + 127) >> 7;
		registers[3] += count;
		registers[1] = address;
#ifdef UJ11_TERAK_RLV12
		registers[4] = address >> 16;
#endif
		registers[0] = (registers[0] & ~060u) | ((address >> 12) & 060);
		registers[2] = original + ((done + 127) >> 7);
	}
	position[unit] = (cylinder << 7) | (head << 6);
	rotation[unit] = sec >= 40 ? 0 : sec;
	return overrun ? error(1) : 0;
}

void rl_service(void)
{
	if (!busy) {
		return;
	}
	generation = epoch;
	BUS_CONTROL = 9;
	unsigned status = command();
	close_card();
	if (valid()) {
		busy = 0;
		registers[0] = (registers[0] & 01776) | 0200 | status;
		rl_irq = !!(registers[0] & 0100);
	}
	BUS_CONTROL = 1;
}
