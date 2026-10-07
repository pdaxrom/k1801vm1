/* TERAK variable-density controller: NEC 765 command/results and byte-counted
 * DMA (50-0010-002 Rev E). Registers use the existing SERV peripheral bridge. */
#include "terak.h"

static uint8_t command[9], result[7], positions[4], seek_status[4], id_index[4];
static unsigned received, needed, results, next_result, pending, interrupt, seeking;
static unsigned dma_control, dma_count, dma_address, count_high, address_high, non_dma;
static u32 epoch, running;

static void publish_irq(void)
{
	/* Original RT-11 QB.SYS header: vector 234, PSW 340 (BR4). */
	BUS_IRQ = interrupt && !(dma_control & 8) ? 0x10000u | 0234 : 0;
}

static void reset_controller(void)
{
	received = needed = results = next_result = pending = interrupt = seeking = 0;
	dma_control = dma_count = dma_address = count_high = address_high = non_dma = 0;
	for (unsigned i = 0; i < 4; i++) {
		positions[i] = seek_status[i] = id_index[i] = 0;
	}
	++epoch;
	BUS_CONTROL = running ? 5 : 1;
	publish_irq();
}

static void response(unsigned size, unsigned irq)
{
	results = size;
	next_result = received = needed = pending = 0;
	interrupt = irq;
	publish_irq();
}

static unsigned command_size(unsigned op)
{
	switch (op & 31) {
	case 2:
	case 5:
	case 6:
	case 9:
	case 12:
	case 17:
	case 25:
	case 29:
		return 9;
	case 3:
	case 15:
		return 3;
	case 4:
	case 7:
	case 10:
		return 2;
	case 8:
		return 1;
	case 13:
		return 6;
	default:
		return 1;
	}
}

void qb_poll(void)
{
	u32 status = BUS_STATUS;
	if (status & 2) {
		reset_controller();
		BUS_STATUS = 2;
	}
	if (status & 4) {
		interrupt = 0;
		BUS_STATUS = 4;
		publish_irq();
	}
	if (!(status & 1)) {
		return;
	}
	u32 request = BUS_REQUEST, address = request & 017776;
	unsigned write = (request >> 13) & 1, value = request >> 16;
	u32 reply = 0;
	if (address != 011000 && address != 011002 && address != 011012 &&
	                address != 011014 && address != 011016) {
		reply = 0x10000;
	} else if (!(request & (1u << 14))) {
		/* The physical controller's registers use only the low byte lane. */
		reply = 0;
	} else if (address == 011000) {
		if (write) {
			reply = 0x10000;
		} else {
			reply = results ? 0xd0 : pending || running ? 0x10 : 0x80 | (received ? 0x10 : 0);
			/* QB.SYS polls RQM/DIO with word MOV, BPL and ROL/BMI.
			 * Expose the status byte on both byte lanes, as its Q-bus port. */
			reply |= reply << 8;
		}
	} else if (address == 011002) {
		if (write) {
			if (!results && !pending && !running) {
				if (!received) {
					needed = command_size(value);
				}
				command[received++] = value;
				if (received == needed) {
					pending = 1;
				}
			}
		} else if (results) {
			reply = result[next_result++];
			if (next_result == results) {
				results = next_result = 0;
				interrupt = 0;
				publish_irq();
			}
		}
	} else if (address == 011012) {
		if (write) {
			dma_control = value & 255;
			publish_irq();
		} else {
			reply = dma_control;
		}
	} else {
		unsigned *reg = address == 011014 ? &dma_count : &dma_address;
		unsigned *high = address == 011014 ? &count_high : &address_high;
		if (write) {
			*reg = *high ? (*reg & 255) | ((value & 255) << 8) : (*reg & 65280) | (value & 255);
		} else {
			reply = (*reg >> (*high ? 8 : 0)) & 255;
		}
		*high ^= 1;
	}
	BUS_RESPONSE = reply;
}

static int valid(void)
{
	return running == epoch + 1;
}

/* Byte edges use the existing direct-word engine; aligned interiors use bulk
 * DMA. Odd addresses/counts retain neighboring RAM and sector bytes. */
static int memory_byte(unsigned address, unsigned offset, unsigned write)
{
	if (address >= 57344 || !valid()) {
		return 0;
	}
	DMA_ADDR = address & ~1u;
	if (transfer(6) & 2) {
		return 0;
	}
	unsigned value = DIRECT, shift = (address & 1) * 8;
	if (write) {
		DIRECT = (value & ~(255u << shift)) | ((u32)sector[offset] << shift);
		if (transfer(7) & 2) {
			return 0;
		}
	} else {
		sector[offset] = value >> shift;
	}
	return valid();
}

static int dma(unsigned offset, unsigned count, unsigned to_memory)
{
	if (dma_address + count > 57344 || !valid()) {
		return 0;
	}
	while (count && ((dma_address | offset) & 1)) {
		if (!memory_byte(dma_address, offset, to_memory)) {
			return 0;
		}
		++dma_address;
		++offset;
		--count;
	}
	unsigned words = count >> 1;
	if (words) {
		DMA_ADDR = dma_address;
		DMA_MODE = 1;
		WINDOW = (offset >> 1) | (((offset >> 1) + words) << 8);
		DMA_COUNT = words;
		if (transfer(to_memory ? STORE : LOAD) & 2 || !valid()) {
			return 0;
		}
		dma_address += words * 2;
		offset += words * 2;
	}
	if (count & 1) {
		if (!memory_byte(dma_address, offset, to_memory)) {
			return 0;
		}
		++dma_address;
	}
	return 1;
}

static void finish(unsigned st0, unsigned st1, unsigned st2, unsigned c, unsigned h, unsigned r, unsigned n)
{
	result[0] = st0;
	result[1] = st1;
	result[2] = st2;
	result[3] = c;
	result[4] = h;
	result[5] = r;
	result[6] = n;
	response(7, 1);
}

void qb_service(void)
{
	if (!pending || running) {
		return;
	}
	running = epoch + 1;
	unsigned op = command[0] & 31, unit = command[1] & 3, head = (command[1] >> 2) & 1;
	unsigned c = command[2], h = command[3], r = command[4], n = command[5];
	unsigned st0 = unit | (head << 2), st1 = 0, st2 = 0;
	unsigned writing = op == 5 || op == 9 || op == 13;
	if (op == 3) {
		non_dma = command[2] & 1;
		response(0, 0);
	} else if (op == 4) {
		result[0] = st0 | (media[unit].start ? 32 : 0) | (media[unit].readonly ? 64 : 0) |
		            (positions[unit] == 0 ? 16 : 0) | (media[unit].heads == 2 ? 8 : 0);
		response(1, 0);
	} else if (op == 7 || op == 15) {
		unsigned target = op == 7 ? 0 : command[2];
		positions[unit] = target < 77 ? target : 76;
		seek_status[unit] = st0 | 32 | (!media[unit].start ? 0x48 : target >= 77 ? 0x50 : 0);
		seeking |= 1u << unit;
		response(0, 1);
	} else if (op == 8) {
		unsigned u;
		for (u = 0; u < 4 && !(seeking & (1u << u)); u++) {
		}
		if (u == 4) {
			result[0] = 128;
			response(1, 0);
		} else {
			result[0] = seek_status[u];
			result[1] = positions[u];
			seeking &= ~(1u << u);
			response(2, 0);
		}
	} else if (op == 10) {
		c = positions[unit];
		h = head;
		if (!track_load(unit, c, head) || track_meta[6] != !!(command[0] & 64)) {
			finish(st0 | 0x40, 5, 0, c, h, 1, 0);
		} else {
			unsigned i = id_index[unit] < track_meta[4] ? id_index[unit] : 0;
			unsigned p = 32 + 8 * i;
			id_index[unit] = i + 1;
			finish(st0, 0, 0, track_meta[p], track_meta[p + 1], track_meta[p + 2], track_meta[p + 3]);
		}
	} else if (op == 2 || op == 5 || op == 6 || op == 9 || op == 12 || op == 13 ||
	                op == 17 || op == 25 || op == 29) {
		unsigned remaining = (dma_count & 16383) + 1;
		unsigned scanning = op == 17 || op == 25 || op == 29;
		unsigned to_memory = !writing && !scanning;
		unsigned expected_direction = to_memory ? 0x4000 : 0x8000;
		if (!media[unit].start || head >= media[unit].heads) {
			st0 |= 0x48;
		} else if (writing && media[unit].readonly) {
			st0 |= 0x40;
			st1 = 2;
		} else if (non_dma || !(dma_control & 1) || (dma_count & 0xc000) != expected_direction ||
		                !!(dma_control & 16) != to_memory) {
			st0 |= 0x40;
			st1 = 16;
		} else if (media[unit].format != MEDIA_FD && (op == 9 || op == 12)) {
			st0 |= 0x40;
			st1 = 4;
		} else if (op == 13) {
			/* Format IDs are supplied by DMA as C,H,R,N quadruples. Track
			 * storage has room for all manufacturer FM/MFM formats <=16 KiB. */
			c = positions[unit];
			h = head;
			n = command[2];
			r = 1;
			unsigned count = command[3];
			if (n > 6 || !count || count > 32 || (count << n) > 128 || remaining < count * 4) {
				st0 |= 0x40;
				st1 = 4;
			} else {
				for (unsigned i = 0; i < 512; i++) {
					sector[i] = 0;
				}
				if (!dma(0, count * 4, 0)) {
					st0 |= 0x40;
					st1 = 16;
				} else {
					for (unsigned i = 0; i < 512; i++) {
						track_meta[i] = 0;
					}
					track_meta[0] = 'T';
					track_meta[1] = 'R';
					track_meta[2] = 'K';
					track_meta[3] = '1';
					track_meta[4] = count;
					track_meta[5] = n;
					track_meta[6] = !!(command[0] & 64);
					for (unsigned i = 0; i < count; i++) {
						for (unsigned b = 0; b < 4; b++) {
							track_meta[32 + 8 * i + b] = sector[4 * i + b];
						}
						track_meta[36 + 8 * i] = 1;
						if (sector[4 * i + 3] != n) {
							st0 |= 0x40;
							st1 = 4;
						}
					}
					if (!track_format(unit, c, h)) {
						st0 |= 0x40;
						st1 = 4;
					}
					u32 position = track_data(unit, c, h);
					unsigned bytes = count * (128u << n);
					for (unsigned done = 0; !st1 && done < bytes;) {
						unsigned offset = (position + done) & 511;
						unsigned chunk = bytes - done < 512 - offset ? bytes - done : 512 - offset;
						u32 lba = media[unit].start + ((position + done) >> 9);
						if ((offset || chunk < 512) && !fetch(lba)) {
							st0 |= 0x40;
							st1 = 16;
							break;
						}
						for (unsigned b = 0; b < chunk; b++) {
							sector[offset + b] = command[5];
						}
						if (!valid() || !store(lba)) {
							st0 |= 0x40;
							st1 = 16;
						}
						done += chunk;
					}
					if (!st1 && !track_save(unit, c, h)) {
						st0 |= 0x40;
						st1 = 16;
					}
					remaining -= count * 4;
					dma_count = (dma_count & 0xc000) | ((remaining - 1) & 16383);
				}
			}
		} else {
			for (;;) {
				if (!valid()) {
					break;
				}
				if (!track_load(unit, positions[unit], head) || track_meta[6] != !!(command[0] & 64)) {
					st0 |= 0x40;
					st1 = 5;
					break;
				}
				unsigned index;
				for (index = 0; index < track_meta[4]; index++) {
					unsigned p = 32 + index * 8;
					if (track_meta[p + 2] == r && track_meta[p + 3] == n) {
						break;
					}
				}
				if (index == track_meta[4]) {
					st0 |= 0x40;
					st1 = 4;
					break;
				}
				unsigned p = 32 + index * 8, flags = track_meta[p + 4];
				if (track_meta[p] != c || track_meta[p + 1] != h || h != head) {
					st0 |= 0x40;
					st1 = 4;
					st2 = 16 | (track_meta[p] == 255 ? 2 : 0);
					break;
				}
				if (!writing && !(flags & 1)) {
					st0 |= 0x40;
					st1 = 1;
					st2 = 1;
					break;
				}
				unsigned deleted = op == 12;
				if (!writing && op != 2 && !!(flags & 2) != deleted && (command[0] & 32)) {
					if (++r > command[6]) {
						st0 |= 0x40;
						st1 = 128;
						break;
					}
					continue;
				}
				unsigned length = n ? 128u << n : command[8] ? command[8] : 128;
				if (length > (128u << n)) {
					length = 128u << n;
				}
				unsigned bytes = remaining < length ? remaining : length;
				u32 position = track_data(unit, positions[unit], head) + index * (128u << n);
				unsigned compare = 0;
				for (unsigned done = 0; done < bytes;) {
					unsigned offset = (position + done) & 511;
					unsigned chunk = bytes - done < 512 - offset ? bytes - done : 512 - offset;
					u32 lba = media[unit].start + ((position + done) >> 9);
					if (!fetch(lba) || !valid()) {
						st0 |= 0x40;
						st1 = 16;
						break;
					}
					if (scanning) {
						for (unsigned b = 0; b < chunk; b++) {
							unsigned disk_byte = sector[offset + b];
							if (!memory_byte(dma_address++, offset + b, 0)) {
								st0 |= 0x40;
								st1 = 16;
								break;
							}
							unsigned memory = sector[offset + b];
							if (disk_byte != 255 && memory != 255 && disk_byte != memory && !compare) {
								compare = disk_byte > memory ? 1 : 2;
							}
						}
					} else if (!dma(offset, chunk, to_memory)) {
						st0 |= 0x40;
						st1 = 16;
					}
					if (st1) {
						break;
					}
					if (writing && !store(lba)) {
						st0 |= 0x40;
						st1 = 16;
						break;
					}
					done += chunk;
					remaining -= chunk;
					dma_count = (dma_count & 0xc000) | ((remaining - 1) & 16383);
				}
				if (st1) {
					break;
				}
				if (writing) {
					track_meta[p + 4] = op == 9 ? 3 : 1;
					if (!track_save(unit, positions[unit], head)) {
						st0 |= 0x40;
						st1 = 16;
						break;
					}
				} else {
					if (flags & 4) {
						st0 |= 0x40;
						st1 = 32;
						st2 |= 32;
					}
					if (op != 2 && !!(flags & 2) != deleted) {
						st0 |= 0x40;
						st2 |= 64;
					}
				}
				if (scanning) {
					unsigned hit = !compare || (op == 25 && compare == 2) || (op == 29 && compare == 1);
					st2 |= !compare ? 8 : hit ? 0 : 4;
					if (hit || !remaining || st1 || st2 & 64) {
						break;
					}
				}
				if (st1 || st2 & 64) {
					break;
				}
				/* Result CHRN describes the successor after terminal count. */
				r += scanning && command[8] == 2 ? 2 : 1;
				if (r > command[6]) {
					r = 1;
					if (command[0] & 128) {
						head ^= 1;
						h = head;
					}
					if (!(command[0] & 128) || head == 0) {
						++c;
					}
					if (remaining) {
						if (!(command[0] & 128) || head == 0) {
							st0 |= 0x40;
							st1 = 128;
							break;
						}
					}
				}
				if (!remaining) {
					break;
				}
			}
		}
		if (valid()) {
			if (!remaining) {
				dma_control |= 1;
			} else {
				dma_control &= ~1u;
			}
			finish(st0, st1, st2, c, h, r, n);
		}
	} else {
		result[0] = 128;
		response(1, 0);
	}
	if (!valid()) {
		pending = received = needed = results = next_result = 0;
	}
	running = 0;
	BUS_CONTROL = 1;
}
