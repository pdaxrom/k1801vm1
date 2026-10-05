#include <string.h>

#include "bk_fdd.h"

enum { TR0 = 1, RDY = 2, WRP = 4, TR = 0200, CRC = 040000, IND = 0100000 };
enum { MOTOR = 020, SIDE = 040, DIRECTION = 0100, STEP = 0200, GOR = 0400, MARK = 01000 };

typedef struct {
	bk_disk_partition partition;
	uint16_t words[BK_FDD_WORDS];
	uint8_t markers[BK_FDD_WORDS];
	unsigned int track;
	unsigned int side;
	int loaded;
	int dirty;
} bk_drive;

static bk_drive drives[4];
static bk_sd_read sd_read;
static bk_sd_write sd_write;
static void *sd_context;
static int selected = -1, motor, synchronizing, marker_found, writing, data_ready, crc_flag, error;
static unsigned int position, phase, last_read, last_write, ready_position, marker_position;

static uint16_t crc_byte(uint16_t crc, uint8_t value)
{
	crc ^= (uint16_t)value << 8;
	for (unsigned int bit = 0; bit < 8; bit++) {
		crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
	}
	return crc;
}

static uint16_t track_crc(const bk_drive *drive, unsigned int start, unsigned int count)
{
	uint16_t crc = 0xffff;
	for (unsigned int word = 0; word < count; word++) {
		uint16_t value = drive->words[(start + word) % BK_FDD_WORDS];
		crc = crc_byte(crc, value >> 8);
		crc = crc_byte(crc, value);
	}
	return crc;
}

static unsigned int distance(unsigned int old, unsigned int next)
{
	return (next + BK_FDD_WORDS - old) % BK_FDD_WORDS;
}

static uint32_t sector_lba(const bk_drive *drive, unsigned int sector)
{
	return drive->partition.base + (drive->track * 2 + drive->side) * 10 + sector - 1;
}

static int flush_drive(bk_drive *drive)
{
	if (!drive->dirty) {
		return 0;
	}
	if (!sd_write || drive->partition.readonly || !drive->partition.length) {
		return -1;
	}
	unsigned int index = 0;
	while (index + 5 < BK_FDD_WORDS) {
		unsigned int header = index++;
		if (!drive->markers[header] || drive->words[header] != 0xa1a1 || drive->words[header + 1] != 0xa1fe) {
			continue;
		}
		unsigned int sector = drive->words[header + 3] >> 8;
		if (drive->words[header + 2] != (drive->track << 8 | drive->side) || sector < 1 || sector > 10 ||
		                (drive->words[header + 3] & 255) != 2 || track_crc(drive, header, 4) != drive->words[header + 4]) {
			continue;
		}
		index = header + 5;
		while (index + 259 <= BK_FDD_WORDS && !drive->markers[index]) {
			index++;
		}
		if (index + 259 > BK_FDD_WORDS) {
			break;
		}
		unsigned int data = index;
		index += 259;
		if (drive->words[data] != 0xa1a1 || drive->words[data + 1] != 0xa1fb ||
		                track_crc(drive, data, 258) != drive->words[data + 258]) {
			continue;
		}
		uint8_t buffer[512];
		for (unsigned int word = 0; word < 256; word++) {
			buffer[word * 2] = drive->words[data + 2 + word] >> 8;
			buffer[word * 2 + 1] = drive->words[data + 2 + word];
		}
		if (sd_write(sd_context, sector_lba(drive, sector), buffer)) {
			return -1;
		}
	}
	drive->dirty = 0;
	return 0;
}

static int load_track(bk_drive *drive)
{
	memset(drive->markers, 0, sizeof(drive->markers));
	for (unsigned int word = 0; word < BK_FDD_WORDS; word++) {
		drive->words[word] = 0x4e4e;
	}
	drive->loaded = 0;
	if (!drive->partition.length || !sd_read) {
		return 0;
	}
	unsigned int index = 16;
	for (unsigned int sector = 1; sector <= 10; sector++) {
		uint8_t buffer[512];
		if (sd_read(sd_context, sector_lba(drive, sector), buffer)) {
			return -1;
		}
		for (unsigned int word = 0; word < 6; word++) {
			drive->words[index++] = 0;
		}
		unsigned int header = index;
		drive->markers[index] = 1;
		drive->words[index++] = 0xa1a1;
		drive->words[index++] = 0xa1fe;
		drive->words[index++] = drive->track << 8 | drive->side;
		drive->words[index++] = sector << 8 | 2;
		drive->words[index] = track_crc(drive, header, 4);
		index += 12;
		for (unsigned int word = 0; word < 6; word++) {
			drive->words[index++] = 0;
		}
		unsigned int data = index;
		drive->markers[index] = 1;
		drive->words[index++] = 0xa1a1;
		drive->words[index++] = 0xa1fb;
		for (unsigned int word = 0; word < 256; word++) {
			drive->words[index++] = buffer[word * 2] << 8 | buffer[word * 2 + 1];
		}
		drive->words[index] = track_crc(drive, data, 258);
		index += sector == 10 ? 24 : 25;
	}
	drive->loaded = 1;
	return 0;
}

int bk_fdd_flush(void)
{
	for (unsigned int drive = 0; drive < 4; drive++) {
		if (flush_drive(&drives[drive])) {
			error = 1;
			return -1;
		}
	}
	return 0;
}

static void cancel_read(void)
{
	synchronizing = marker_found = data_ready = crc_flag = 0;
}

static void end_write(void)
{
	if (writing) {
		if (selected >= 0 && flush_drive(&drives[selected])) {
			error = 1;
		}
		writing = 0;
		crc_flag = 0;
	}
}

void bk_fdd_reset(void)
{
	end_write();
	selected = -1;
	motor = 0;
	cancel_read();
	last_read = last_write = ready_position = marker_position = position;
}

int bk_fdd_configure(uint32_t capacity, uint32_t boot_base, uint32_t boot_length,
                     const bk_disk_partition partitions[4], bk_sd_read read_sector,
                     bk_sd_write write_sector, void *context)
{
	uint64_t boot_end = (uint64_t)boot_base + boot_length;
	if (!capacity || !boot_base || !boot_length || boot_end > capacity || !partitions || !read_sector) {
		return -1;
	}
	for (unsigned int drive = 0; drive < 4; drive++) {
		const bk_disk_partition *partition = &partitions[drive];
		uint64_t end = (uint64_t)partition->base + partition->length;
		if (!partition->length && !partition->base) {
			continue;
		}
		if (!partition->base || partition->length < BK_FDD_SECTORS || end > capacity ||
		                (partition->base < boot_end && end > boot_base)) {
			return -1;
		}
		for (unsigned int other = 0; other < drive; other++) {
			if (partitions[other].length && partition->base < (uint64_t)partitions[other].base + partitions[other].length &&
			                end > partitions[other].base) {
				return -1;
			}
		}
	}
	if (bk_fdd_flush()) {
		return -1;
	}
	memset(drives, 0, sizeof(drives));
	for (unsigned int drive = 0; drive < 4; drive++) {
		drives[drive].partition = partitions[drive];
	}
	sd_read = read_sector;
	sd_write = write_sector;
	sd_context = context;
	error = 0;
	bk_fdd_reset();
	return 0;
}

void bk_fdd_advance(unsigned int cycles, unsigned int mhz)
{
	if (!mhz || 48 % mhz) {
		return;
	}
	uint64_t ticks = phase + (uint64_t)cycles * (48 / mhz);
	position = (position + ticks / 3072) % BK_FDD_WORDS;
	phase = ticks % 3072;
}

uint16_t bk_fdd_read(unsigned int address)
{
	/* Match the fixed 8 us FPGA CSR wait; its ROM CRC polling must span a 64 us word even at 24 MHz. */
	if (address == 0177130) {
		bk_fdd_advance(384, 48);
	}
	if (selected < 0) {
		return 0;
	}
	bk_drive *drive = &drives[selected];
	if (address == 0177132) {
		end_write();
		uint16_t result = motor ? drive->words[data_ready ? ready_position : position] : 0;
		data_ready = 0;
		last_read = position;
		return result;
	}
	uint16_t result = drive->track == 0 ? TR0 : 0;
	if (drive->partition.length && drive->partition.readonly) {
		result |= WRP;
	}
	if (motor && drive->loaded) {
		result |= RDY;
	}
	if (motor && drive->loaded && position < 16) {
		result |= IND;
	}
	if (writing) {
		unsigned int count = distance(last_write, position);
		data_ready = count > 0;
		if (count > 1 && !crc_flag) {
			unsigned int next = (last_write + 1) % BK_FDD_WORDS;
			drive->words[next] = track_crc(drive, marker_position, distance(marker_position, next));
			drive->markers[next] = 0;
			crc_flag = 1;
		}
		if (count > 2) {
			end_write();
		}
	}
	if (!writing && motor && drive->loaded) {
		unsigned int count = distance(last_read, position);
		if (marker_found && !data_ready && count) {
			data_ready = 1;
			ready_position = (last_read + 1) % BK_FDD_WORDS;
		}
		if (synchronizing) {
			if (!marker_found && drive->markers[position]) {
				marker_found = data_ready = 1;
				marker_position = ready_position = position;
				last_read = position;
			} else if (marker_found && count > 1) {
				synchronizing = 0;
				crc_flag = track_crc(drive, marker_position, distance(marker_position, ready_position)) ==
				           drive->words[ready_position];
			}
		}
	}
	return result | (data_ready ? TR : 0) | (crc_flag ? CRC : 0);
}

void bk_fdd_write(unsigned int address, uint16_t value)
{
	if (address == 0177132) {
		if (selected >= 0 && motor && drives[selected].loaded && !drives[selected].partition.readonly) {
			if (!writing) {
				cancel_read();
				writing = 1;
			}
			drives[selected].words[position] = (value << 8) | (value >> 8);
			drives[selected].markers[position] = 0;
			drives[selected].dirty = 1;
		}
		data_ready = crc_flag = 0;
		last_write = position;
		return;
	}
	int next = -1;
	for (unsigned int drive = 0; drive < 4; drive++) {
		if (value & (1 << drive)) {
			next = drive;
			break;
		}
	}
	if (next != selected) {
		end_write();
		cancel_read();
	}
	selected = next;
	motor = (value & MOTOR) != 0;
	if (selected < 0) {
		return;
	}
	bk_drive *drive = &drives[selected];
	if ((value & MARK) && writing) {
		drive->markers[last_write] = 1;
		marker_position = last_write;
	}
	unsigned int track = drive->track;
	unsigned int side = (value & SIDE) != 0;
	if (value & STEP) {
		if (value & DIRECTION) {
			if (track < 79) {
				track++;
			}
		} else if (track) {
			track--;
		}
	}
	if (!drive->loaded || track != drive->track || side != drive->side) {
		end_write();
		cancel_read();
		drive->track = track;
		drive->side = side;
		if (load_track(drive)) {
			error = 1;
		}
	}
	if (value & GOR) {
		end_write();
		cancel_read();
		synchronizing = 1;
	}
}

int bk_fdd_error(void)
{
	return error;
}
